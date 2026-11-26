/* tinydaw MIDI input. On Linux a thread waits on the ALSA sequencer, connects itself to every MIDI
 * output port (including ones plugged in later) and reports notes and controller presses. */
#include "midi.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)

/* Windows: the built-in MIDI API (winmm). Windows sends no message when a MIDI device is
 * plugged in, so a thread looks for new devices every two seconds. */
#include <windows.h>
#include <mmsystem.h>

static midi_fn callback;
static void *user;
static char error[160] = "not started";
static CRITICAL_SECTION mu;
static HANDLE watcher;
static volatile LONG quit;
static unsigned char cc_down[16][128];

static struct { HMIDIIN h; char name[64]; } devs[MIDI_MAX_DEVICES];
static int ndevs;

static void CALLBACK on_midi(HMIDIIN h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2)
{
    (void)h;
    (void)p2;
    if (msg != MIM_DATA) return;
    int dev = (int)inst, status = p1 & 0xF0, ch = p1 & 0x0F, d1 = (p1 >> 8) & 0x7F, d2 = (p1 >> 16) & 0x7F;
    if (status == 0x90) callback(d1, d2, dev, user);           /* velocity 0 = off */
    else if (status == 0x80) callback(d1, 0, dev, user);
    else if (status == 0xE0) callback(MIDI_BEND, d1 | d2 << 7, dev, user);
    else if (status == 0xB0) {
        callback(MIDI_CCV + d1, d2, dev, user);
        int down = d2 >= 64;
        if (down != cc_down[ch][d1]) {
            cc_down[ch][d1] = (unsigned char)down;
            callback(MIDI_CC + d1, down ? d2 : 0, dev, user);
        }
    }
}

static void close_all(void)
{
    for (int i = 0; i < ndevs; i++) {
        midiInStop(devs[i].h);
        midiInClose(devs[i].h);
    }
    ndevs = 0;
}

static void open_all(void)
{
    UINT n = midiInGetNumDevs();
    for (UINT i = 0; i < n && ndevs < MIDI_MAX_DEVICES; i++) {
        MIDIINCAPSW caps;
        HMIDIIN h;
        if (midiInGetDevCapsW(i, &caps, sizeof caps) != MMSYSERR_NOERROR) continue;
        int slot = ndevs;
        if (midiInOpen(&h, i, (DWORD_PTR)on_midi, (DWORD_PTR)slot, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
            continue;                         /* usually: another program has it open */
        devs[slot].h = h;
        WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, devs[slot].name, 64, NULL, NULL);
        midiInStart(h);
        ndevs++;
    }
}

static UINT seen_count(void) { return midiInGetNumDevs(); }

static DWORD WINAPI watch(LPVOID arg)
{
    (void)arg;
    UINT last = seen_count();
    while (!quit) {
        Sleep(2000);
        UINT now = seen_count();
        if (now != last) {                      /* plugged in or out: open them all again */
            EnterCriticalSection(&mu);
            close_all();
            open_all();
            LeaveCriticalSection(&mu);
            last = now;
        }
    }
    return 0;
}

int midi_start(midi_fn fn, void *ud)
{
    callback = fn;
    user = ud;
    InitializeCriticalSection(&mu);
    EnterCriticalSection(&mu);
    open_all();
    LeaveCriticalSection(&mu);
    watcher = CreateThread(NULL, 0, watch, NULL, 0, NULL);
    if (!watcher) {
        snprintf(error, sizeof error, "could not start the MIDI thread");
        return -1;
    }
    return 0;
}

void midi_stop(void)
{
    if (!watcher) return;
    InterlockedExchange(&quit, 1);
    WaitForSingleObject(watcher, 3000);
    CloseHandle(watcher);
    watcher = NULL;
    EnterCriticalSection(&mu);
    close_all();
    LeaveCriticalSection(&mu);
}

const char *midi_error(void) { return error; }

int midi_devices(char names[][64], int max)
{
    if (!watcher) return 0;
    EnterCriticalSection(&mu);
    int n = ndevs < max ? ndevs : max;
    for (int i = 0; i < n; i++) snprintf(names[i], 64, "%s", devs[i].name);
    LeaveCriticalSection(&mu);
    return n;
}

#elif !defined(HAVE_ALSA)

int midi_start(midi_fn fn, void *ud) { (void)fn; (void)ud; return -1; }
void midi_stop(void) {}
const char *midi_error(void) { return "built without ALSA (install alsa-lib headers and rebuild)"; }
int midi_devices(char names[][64], int max) { (void)names; (void)max; return 0; }

#else

#include <alsa/asoundlib.h>
#include <poll.h>
#include <pthread.h>

static snd_seq_t *seq;
static int my_port = -1;
static pthread_t thread;
static volatile int quit;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static midi_fn callback;
static void *user;
static char error[160] = "not started";
static unsigned char cc_down[16][128];

static struct { int client, port; char name[64]; } devs[MIDI_MAX_DEVICES];
static int ndevs;

static int find_dev(int client, int port)
{
    for (int i = 0; i < ndevs; i++)
        if (devs[i].client == client && devs[i].port == port) return i;
    return -1;
}

static void connect_port(int client, int port)
{
    if (client == SND_SEQ_CLIENT_SYSTEM || client == snd_seq_client_id(seq)) return;
    snd_seq_port_info_t *pi;
    snd_seq_client_info_t *ci;
    snd_seq_port_info_alloca(&pi);
    snd_seq_client_info_alloca(&ci);
    if (snd_seq_get_any_port_info(seq, client, port, pi) < 0) return;
    if (snd_seq_get_any_client_info(seq, client, ci) < 0) return;
    unsigned caps = snd_seq_port_info_get_capability(pi), type = snd_seq_port_info_get_type(pi);
    unsigned need = SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;
    if ((caps & need) != need || (caps & SND_SEQ_PORT_CAP_NO_EXPORT)) return;
    if (!(type & SND_SEQ_PORT_TYPE_MIDI_GENERIC)) return;
    const char *cname = snd_seq_client_info_get_name(ci), *pname = snd_seq_port_info_get_name(pi);
    if (strstr(cname, "Midi Through")) return;              /* the kernel's loopback, not a device */
    if (snd_seq_connect_from(seq, my_port, client, port) < 0) return;
    pthread_mutex_lock(&mu);
    if (find_dev(client, port) < 0 && ndevs < MIDI_MAX_DEVICES) {
        devs[ndevs].client = client;
        devs[ndevs].port = port;
        if (!strncmp(pname, cname, strlen(cname))) snprintf(devs[ndevs].name, 64, "%s", pname);
        else snprintf(devs[ndevs].name, 64, "%s: %s", cname, pname);
        ndevs++;
    }
    pthread_mutex_unlock(&mu);
}

static void drop_port(int client, int port)         /* port < 0: every port of the client */
{
    pthread_mutex_lock(&mu);
    for (int i = 0; i < ndevs; i++) {
        if (devs[i].client == client && (port < 0 || devs[i].port == port)) {
            devs[i] = devs[--ndevs];
            i--;
        }
    }
    pthread_mutex_unlock(&mu);
}

static void scan(void)
{
    snd_seq_client_info_t *ci;
    snd_seq_port_info_t *pi;
    snd_seq_client_info_alloca(&ci);
    snd_seq_port_info_alloca(&pi);
    snd_seq_client_info_set_client(ci, -1);
    while (snd_seq_query_next_client(seq, ci) >= 0) {
        int client = snd_seq_client_info_get_client(ci);
        snd_seq_port_info_set_client(pi, client);
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(seq, pi) >= 0) connect_port(client, snd_seq_port_info_get_port(pi));
    }
}

static void handle(const snd_seq_event_t *ev)
{
    int dev = -1;
    pthread_mutex_lock(&mu);
    dev = find_dev(ev->source.client, ev->source.port);
    pthread_mutex_unlock(&mu);
    switch (ev->type) {
    case SND_SEQ_EVENT_NOTEON:
        callback(ev->data.note.note & 127, ev->data.note.velocity, dev, user);   /* velocity 0 = off */
        break;
    case SND_SEQ_EVENT_NOTEOFF:
        callback(ev->data.note.note & 127, 0, dev, user);
        break;
    case SND_SEQ_EVENT_PITCHBEND:
        callback(MIDI_BEND, ev->data.control.value + 8192, dev, user);
        break;
    case SND_SEQ_EVENT_CONTROLLER: {                  /* knobs, and pads and buttons that send CCs */
        int ch = ev->data.control.channel & 15, cc = ev->data.control.param & 127;
        callback(MIDI_CCV + cc, ev->data.control.value & 127, dev, user);
        int down = ev->data.control.value >= 64;
        if (down != cc_down[ch][cc]) {
            cc_down[ch][cc] = (unsigned char)down;
            callback(MIDI_CC + cc, down ? ev->data.control.value : 0, dev, user);
        }
        break;
    }
    case SND_SEQ_EVENT_PORT_START:
        connect_port(ev->data.addr.client, ev->data.addr.port);
        break;
    case SND_SEQ_EVENT_PORT_EXIT:
        drop_port(ev->data.addr.client, ev->data.addr.port);
        break;
    case SND_SEQ_EVENT_CLIENT_EXIT:
        drop_port(ev->data.addr.client, -1);
        break;
    }
}

static void *run(void *arg)
{
    (void)arg;
    int n = snd_seq_poll_descriptors_count(seq, POLLIN);
    struct pollfd fds[8];
    if (n > 8) n = 8;
    snd_seq_poll_descriptors(seq, fds, (unsigned)n, POLLIN);
    while (!quit) {
        if (poll(fds, (nfds_t)n, 200) <= 0) continue;
        snd_seq_event_t *ev;
        while (snd_seq_event_input(seq, &ev) >= 0 && ev) handle(ev);
    }
    return NULL;
}

int midi_start(midi_fn fn, void *ud)
{
    callback = fn;
    user = ud;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK) < 0) {
        snprintf(error, sizeof error, "no ALSA sequencer (is the snd-seq module loaded?)");
        seq = NULL;
        return -1;
    }
    snd_seq_set_client_name(seq, "tinydaw");
    my_port = snd_seq_create_simple_port(seq, "tinydaw in", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                         SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (my_port < 0) {
        snprintf(error, sizeof error, "could not create a MIDI input port");
        snd_seq_close(seq);
        seq = NULL;
        return -1;
    }
    /* hear about devices that appear or vanish */
    snd_seq_connect_from(seq, my_port, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
    scan();
    if (pthread_create(&thread, NULL, run, NULL) != 0) {
        snprintf(error, sizeof error, "could not start the MIDI thread");
        snd_seq_close(seq);
        seq = NULL;
        return -1;
    }
    return 0;
}

void midi_stop(void)
{
    if (!seq) return;
    quit = 1;
    pthread_join(thread, NULL);
    snd_seq_close(seq);
    seq = NULL;
}

const char *midi_error(void) { return error; }

int midi_devices(char names[][64], int max)
{
    pthread_mutex_lock(&mu);
    int n = ndevs < max ? ndevs : max;
    for (int i = 0; i < n; i++) snprintf(names[i], 64, "%s", devs[i].name);
    pthread_mutex_unlock(&mu);
    return n;
}

#endif
