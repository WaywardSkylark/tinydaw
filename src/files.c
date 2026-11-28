/* tinydaw: project files, file dialogs, recent projects, the project browser. */
#include "ui.h"

/* ---------------------------------------------------------------- project files */

int recovered;                   /* opened from the autosave: unsaved by definition */

int autosaved_serial = -1;
Uint32 last_autosave;
int confirm_act;                 /* an action waiting on "save changes?" */
int after_save_act;              /* an action to run once a Save As completes */
char act_path[1024];
int menu_open, quit_now;

void mark_saved(void)
{
    state_capture(&saved);
    saved_nsamples = P.nsamples;
    recovered = 0;
}

int is_dirty(void)
{
    return recovered || P.nsamples != saved_nsamples || !state_same(&saved);
}

const char *base_name(const char *path) { return path_base(path); }

const char *dirname_of(const char *path)
{
    static char buf[1024];
    snprintf(buf, sizeof buf, "%s", path);
    char *base = (char *)path_base(buf);
    if (base == buf) snprintf(buf, sizeof buf, ".");
    else if (base == buf + 1) buf[1] = 0;      /* "/file" -> "/" */
    else base[-1] = 0;
    return buf;
}

void ensure_dir(const char *path) { make_dirs(path); }

void projects_dir(char *buf, size_t n)
{
    char music[900];
    user_dir("music", music, sizeof music);
    snprintf(buf, n, "%.880s/tinydaw", music);
    ensure_dir(buf);
}

const char *autosave_file(void)
{
    static char path[1024];
    if (!path[0]) {
        char dir[900];
        user_dir("data", dir, sizeof dir);
        ensure_dir(dir);
        snprintf(path, sizeof path, "%s/autosave.tdaw", dir);
    }
    return path;
}

int autosave_exists(void) { return file_exists(autosave_file()); }

void update_title(void)
{
    static char last[1100];
    char t[1100];
    snprintf(t, sizeof t, "%s%s - tinydaw " TINYDAW_VERSION, is_dirty() ? "* " : "",
             proj_path[0] ? base_name(proj_path) : recovered ? "recovered work" : "untitled");
    if (strcmp(t, last)) {
        snprintf(last, sizeof last, "%s", t);
        SDL_SetWindowTitle(win, t);
    }
}

int save_to(const char *path)
{
    char msg[1200];
    if (project_save(&P, path, msg, sizeof msg) != 0) {
        status("Save failed: %s", msg);
        return -1;
    }
    char *full = full_path(path);
    snprintf(proj_path, sizeof proj_path, "%s", full ? full : path);
    free(full);
    recent_add(proj_path);
    mark_saved();
    remove(autosave_file());            /* everything is in the real file now */
    status("%s", msg);
    return 0;
}

void replace_project(Project *np)
{
    play_scratch(NULL, 0);
    Project *old = malloc(sizeof *old);
    if (!old) return;
    LOCK();
    memcpy(old, &P, sizeof P);
    memcpy(&P, np, sizeof P);
    UNLOCK();
    project_free(old);
    free(old);
    free(np);
    sel_clear();
    tempo_from = 0;
    view_start = 0;
    lib_scroll = 0;
    drag.kind = D_NONE;
    saved_sync();                               /* the saved sounds are in every project */
    history_reset();
}

int open_project(const char *path, int as_recovery)
{
    Project *np = malloc(sizeof *np);
    char msg[1200];
    int old_rate = g_rate;
    if (!np) return -1;
    if (project_load(np, path, msg, sizeof msg) != 0) {
        free(np);
        status("Could not open: %s", msg);
        return -1;
    }
    replace_project(np);
    if (g_rate != old_rate) open_audio();
    mark_saved();
    if (as_recovery) {
        proj_path[0] = 0;
        recovered = 1;
        status("Recovered your unsaved work. Save it with Ctrl+S.");
    } else {
        char *full = full_path(path);
        snprintf(proj_path, sizeof proj_path, "%s", full ? full : path);
        free(full);
        recent_add(proj_path);
        status("%s", msg);
    }
    return 0;
}

void new_project(void)
{
    Project *np = malloc(sizeof *np);
    if (!np) return;
    project_init(np);
    replace_project(np);
    proj_path[0] = 0;
    mark_saved();
    remove(autosave_file());
    status("New project");
}

void autosave_tick(void)
{
    Uint32 now = SDL_GetTicks();
    int serial = edit_serial + P.nsamples * 100000;
    if (now - last_autosave < 30000 || serial == autosaved_serial || !is_dirty()) return;
    char msg[1200];
    last_autosave = now;
    if (project_save(&P, autosave_file(), msg, sizeof msg) == 0) autosaved_serial = serial;
}

/* ---- the system file dialog (zenity or kdialog), run on a thread so the window stays live */

SDL_atomic_t fd_state;           /* 0 idle, 1 open, 2 finished */

int fd_saving;

int fd_export;                   /* 0 a project, 1 where an export goes, 2 a MIDI file to import, 3 to export to,
                                         * 4 a sound for a session slot */

char fd_result[1024];

#ifndef _WIN32

char fd_cmd[4000];

#endif

#ifdef _WIN32

char fd_start[2100];
HWND fd_owner;

/* The standard Windows Open/Save dialog. Paths go in and out as UTF-8. */
void win_file_dialog(void)
{
    wchar_t file[1024] = L"", dir[1024] = L"";
    char d[2100], name[512];
    snprintf(d, sizeof d, "%s", fd_start);
    for (char *c = d; *c; c++)
        if (*c == '/') *c = '\\';
    char *b = (char *)path_base(d);
    snprintf(name, sizeof name, "%s", b);
    if (b > d) b[-1] = 0;
    else d[0] = 0;
    MultiByteToWideChar(CP_UTF8, 0, name, -1, file, 1024);
    MultiByteToWideChar(CP_UTF8, 0, d, -1, dir, 1024);
    OPENFILENAMEW o;
    memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.hwndOwner = fd_owner;
    wchar_t filter[256], ext[16];
    char f8[256];
    const char *e8 = fd_export == 1 ? EXPORT_FORMATS[EXO.format].ext : fd_export == 4 ? "wav" : fd_export >= 2 ? "mid" : "tdaw";
    if (fd_export == 1) snprintf(f8, sizeof f8, "%s audio (*.%s)|*.%s|All files|*.*|", EXPORT_FORMATS[EXO.format].name, e8, e8);
    else if (fd_export == 4) snprintf(f8, sizeof f8, "Audio files|*.wav;*.flac;*.ogg;*.mp3;*.aif;*.aiff|All files|*.*|");
    else if (fd_export >= 2) snprintf(f8, sizeof f8, "MIDI files (*.mid)|*.mid;*.midi|All files|*.*|");
    else snprintf(f8, sizeof f8, "tinydaw projects (*.tdaw)|*.tdaw|All files|*.*|");
    int fl = MultiByteToWideChar(CP_UTF8, 0, f8, -1, filter, 255);
    for (int i = 0; i < fl; i++)
        if (filter[i] == L'|') filter[i] = 0;     /* the list is NUL-separated, ending in two */
    MultiByteToWideChar(CP_UTF8, 0, e8, -1, ext, 16);
    o.lpstrFilter = filter;
    o.lpstrFile = file;
    o.nMaxFile = 1024;
    o.lpstrInitialDir = dir[0] ? dir : NULL;
    o.lpstrDefExt = ext;
    o.lpstrTitle = fd_export == 1 ? L"Export to" : fd_export == 2 ? L"Import a MIDI file" : fd_export == 3 ? L"Export MIDI"
                 : fd_export == 4 ? L"Import a sound into the slot"
                 : fd_saving ? L"Save project" : L"Open project";
    o.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
              (fd_saving ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    BOOL ok = fd_saving ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o);
    fd_result[0] = 0;
    if (ok) WideCharToMultiByte(CP_UTF8, 0, file, -1, fd_result, sizeof fd_result, NULL, NULL);
}

#else

int have_program(const char *name)
{
    const char *path = getenv("PATH");
    char buf[1200];
    if (!path) path = "/usr/bin:/bin";
    while (*path) {
        size_t n = strcspn(path, ":");
        snprintf(buf, sizeof buf, "%.*s/%s", (int)n, path, name);
        if (access(buf, X_OK) == 0) return 1;
        path += n + (path[n] == ':');
    }
    return 0;
}

void shell_quote(char *out, size_t n, const char *s)
{
    size_t k = 0;
    if (k < n - 1) out[k++] = '\'';
    for (; *s && k < n - 5; s++) {
        if (*s == '\'') {
            memcpy(out + k, "'\\''", 4);
            k += 4;
        } else {
            out[k++] = *s;
        }
    }
    out[k++] = '\'';
    out[k] = 0;
}

#endif

int fd_thread(void *arg)
{
    (void)arg;
    fd_result[0] = 0;
#ifdef _WIN32
    win_file_dialog();
#else
    FILE *pp = popen(fd_cmd, "r");
    if (pp) {
        if (!fgets(fd_result, sizeof fd_result, pp)) fd_result[0] = 0;
        pclose(pp);
    }
#endif
    fd_result[strcspn(fd_result, "\r\n")] = 0;
    SDL_AtomicSet(&fd_state, 2);
    return 0;
}

void file_dialog(int saving)
{
    if (SDL_AtomicGet(&fd_state) != 0) return;
    char dir[900], start[2100], q[2400];
    projects_dir(dir, sizeof dir);
    if (saving)
        snprintf(start, sizeof start, "%s/%s", proj_path[0] ? dirname_of(proj_path) : dir,
                 proj_path[0] ? base_name(proj_path) : "untitled.tdaw");
    else
        snprintf(start, sizeof start, "%s/", proj_path[0] ? dirname_of(proj_path) : dir);
    fd_saving = saving;
    fd_export = 0;
#ifdef _WIN32
    (void)q;
    snprintf(fd_start, sizeof fd_start, "%s", start);
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    fd_owner = SDL_GetWindowWMInfo(win, &wm) ? wm.info.win.window : NULL;
    if (0) {
#else
    shell_quote(q, sizeof q, start);
    if (have_program("zenity")) {
        snprintf(fd_cmd, sizeof fd_cmd,
                 "zenity --file-selection %s --title=%s --filename=%s "
                 "--file-filter='tinydaw projects (*.tdaw) | *.tdaw' --file-filter='All files | *' 2>/dev/null",
                 saving ? "--save" : "", saving ? "'Save project'" : "'Open project'", q);
    } else if (have_program("kdialog")) {
        snprintf(fd_cmd, sizeof fd_cmd, "kdialog %s %s 'tinydaw projects (*.tdaw)' 2>/dev/null",
                 saving ? "--getsavefilename" : "--getopenfilename", q);
    } else if (saving) {                     /* no dialog program: save with a dated name */
        char stamp[32], path[1100];
        time_t now = time(NULL);
        strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&now));
        snprintf(path, sizeof path, "%s/project-%s.tdaw", dir, stamp);
        int act = after_save_act;
        after_save_act = ACT_NONE;
        if (save_to(path) == 0 && act) run_action(act);   /* carry on with what the save was for */
        return;
    } else {
        status("No file dialog available (install zenity): drop a .tdaw file on the window, "
               "or run tinydaw file.tdaw");
        return;
#endif
    }
    SDL_AtomicSet(&fd_state, 1);
    SDL_Thread *t = SDL_CreateThread(fd_thread, "filedialog", NULL);
    if (t) SDL_DetachThread(t);
    else SDL_AtomicSet(&fd_state, 0);
}

/* The system Save dialog for an export: starts at the folder and name in the dialog, and
 * hands back both (file_dialog_poll). Without a dialog program the fields can still be typed in. */
void export_file_dialog(void)
{
    if (SDL_AtomicGet(&fd_state) != 0) return;
    const ExportFormat *f = &EXPORT_FORMATS[EXO.format];
    char start[1300];
    export_make_base();
    snprintf(start, sizeof start, "%s.%s", export_base_path, f->ext);
    fd_saving = 1;
#ifdef _WIN32
    snprintf(fd_start, sizeof fd_start, "%s", start);
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    fd_owner = SDL_GetWindowWMInfo(win, &wm) ? wm.info.win.window : NULL;
#else
    char q[2700];
    shell_quote(q, sizeof q, start);
    if (have_program("zenity")) {
        snprintf(fd_cmd, sizeof fd_cmd,
                 "zenity --file-selection --save --confirm-overwrite --title='Export to' --filename=%s "
                 "--file-filter='%s audio (*.%s) | *.%s' --file-filter='All files | *' 2>/dev/null",
                 q, f->name, f->ext, f->ext);
    } else if (have_program("kdialog")) {
        snprintf(fd_cmd, sizeof fd_cmd, "kdialog --getsavefilename %s '*.%s' 2>/dev/null", q, f->ext);
    } else {
        status("No file dialog available (install zenity): type the folder and name instead");
        return;
    }
#endif
    fd_export = 1;
    SDL_AtomicSet(&fd_state, 1);
    SDL_Thread *t = SDL_CreateThread(fd_thread, "filedialog", NULL);
    if (t) SDL_DetachThread(t);
    else {
        SDL_AtomicSet(&fd_state, 0);
        fd_export = 0;
    }
}

/* The system dialog for a MIDI file: to import (open) or export (save). */
void midi_file_dialog(int save)
{
    if (SDL_AtomicGet(&fd_state) != 0) return;
    char start[1300];
    if (!export_dir[0]) {
        user_dir("music", export_dir, sizeof export_dir);
        if (!dir_writable(export_dir) && !getcwd(export_dir, sizeof export_dir)) snprintf(export_dir, sizeof export_dir, ".");
    }
    if (save) {
        char name[256];
        snprintf(name, sizeof name, "%.250s", proj_path[0] ? path_base(proj_path) : "tinydaw.tdaw");
        size_t n = strlen(name);
        if (n > 5 && !strcmp(name + n - 5, ".tdaw")) name[n - 5] = 0;
        snprintf(start, sizeof start, "%s/%s.mid", export_dir, name);
    } else {
        snprintf(start, sizeof start, "%s/", export_dir);
    }
    fd_saving = save;
#ifdef _WIN32
    snprintf(fd_start, sizeof fd_start, "%s", start);
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    fd_owner = SDL_GetWindowWMInfo(win, &wm) ? wm.info.win.window : NULL;
#else
    char q[2700];
    shell_quote(q, sizeof q, start);
    if (have_program("zenity")) {
        snprintf(fd_cmd, sizeof fd_cmd,
                 "zenity --file-selection %s --title=%s --filename=%s "
                 "--file-filter='MIDI files | *.mid *.midi *.MID' --file-filter='All files | *' 2>/dev/null",
                 save ? "--save --confirm-overwrite" : "", save ? "'Export MIDI'" : "'Import a MIDI file'", q);
    } else if (have_program("kdialog")) {
        snprintf(fd_cmd, sizeof fd_cmd, "kdialog %s %s '*.mid *.midi' 2>/dev/null",
                 save ? "--getsavefilename" : "--getopenfilename", q);
    } else if (save) {                      /* no dialog: next to the audio exports */
        midi_export(start);
        return;
    } else {
        status("No file dialog available (install zenity): drop a .mid file on the window instead");
        return;
    }
#endif
    fd_export = save ? 3 : 2;
    SDL_AtomicSet(&fd_state, 1);
    SDL_Thread *t = SDL_CreateThread(fd_thread, "filedialog", NULL);
    if (t) SDL_DetachThread(t);
    else {
        SDL_AtomicSet(&fd_state, 0);
        fd_export = 0;
    }
}

/* a chosen export file: its folder and name go into the dialog (its extension picks the format) */
void export_take_path(const char *path)
{
    char name[512];
    snprintf(name, sizeof name, "%s", path_base(path));
    export_take_ext(name);
    snprintf(export_name, sizeof export_name, "%.255s", name);
    snprintf(export_dir, sizeof export_dir, "%.899s", dirname_of(path));
}

int slot_import_t = -1, slot_import_s = -1;   /* the slot a chosen sound goes into */

/* the system's Open dialog for a sound to put in a session slot */
void slot_import_dialog(int t, int s)
{
    if (SDL_AtomicGet(&fd_state) != 0) return;
    slot_import_t = t;
    slot_import_s = s;
    char start[1300];
    if (!export_dir[0]) {
        user_dir("music", export_dir, sizeof export_dir);
        if (!dir_writable(export_dir) && !getcwd(export_dir, sizeof export_dir)) snprintf(export_dir, sizeof export_dir, ".");
    }
    snprintf(start, sizeof start, "%s/", export_dir);
    fd_saving = 0;
#ifdef _WIN32
    snprintf(fd_start, sizeof fd_start, "%s", start);
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    fd_owner = SDL_GetWindowWMInfo(win, &wm) ? wm.info.win.window : NULL;
#else
    char q[2700];
    shell_quote(q, sizeof q, start);
    if (have_program("zenity")) {
        snprintf(fd_cmd, sizeof fd_cmd,
                 "zenity --file-selection --title='Import a sound into the slot' --filename=%s "
                 "--file-filter='Audio files | *.wav *.flac *.ogg *.mp3 *.aif *.aiff *.WAV *.FLAC *.OGG *.MP3' "
                 "--file-filter='All files | *' 2>/dev/null", q);
    } else if (have_program("kdialog")) {
        snprintf(fd_cmd, sizeof fd_cmd, "kdialog --getopenfilename %s '*.wav *.flac *.ogg *.mp3 *.aif *.aiff' 2>/dev/null", q);
    } else {
        status("No file dialog available (install zenity): drop a sound file on the slot instead");
        return;
    }
#endif
    fd_export = 4;
    SDL_AtomicSet(&fd_state, 1);
    SDL_Thread *th = SDL_CreateThread(fd_thread, "filedialog", NULL);
    if (th) SDL_DetachThread(th);
    else {
        SDL_AtomicSet(&fd_state, 0);
        fd_export = 0;
    }
}

void file_dialog_poll(void)
{
    if (SDL_AtomicGet(&fd_state) != 2) return;
    SDL_AtomicSet(&fd_state, 0);
    if (fd_export == 4) {                   /* a sound for a session slot */
        fd_export = 0;
        if (fd_result[0]) slot_import_path(slot_import_t, slot_import_s, fd_result);
        return;
    }
    if (fd_export >= 2) {                   /* a MIDI file */
        int k = fd_export;
        fd_export = 0;
        if (fd_result[0]) {
            if (k == 2) midi_import(fd_result);
            else {
                char path[1100];
                size_t n = strlen(fd_result);
                snprintf(path, sizeof path, "%s%s", fd_result,
                         (n > 4 && !strcasecmp(fd_result + n - 4, ".mid")) || (n > 5 && !strcasecmp(fd_result + n - 5, ".midi")) ? "" : ".mid");
                midi_export(path);
            }
        }
        return;
    }
    if (fd_export) {                        /* Export > Choose: back to the export dialog */
        fd_export = 0;
        if (fd_result[0]) export_take_path(fd_result);
        return;
    }
    if (!fd_result[0]) {                    /* cancelled */
        after_save_act = ACT_NONE;
        return;
    }
    if (fd_saving) {
        char path[1100];
        size_t n = strlen(fd_result);
        snprintf(path, sizeof path, "%s%s", fd_result, n > 5 && !strcmp(fd_result + n - 5, ".tdaw") ? "" : ".tdaw");
        int act = after_save_act;
        after_save_act = ACT_NONE;
        if (save_to(path) == 0 && act) run_action(act);
    } else {
        open_project(fd_result, 0);
    }
}

void save_project(int as)
{
    if (!as && proj_path[0]) save_to(proj_path);
    else file_dialog(1);
}

/* Runs an action that would lose unsaved changes, asking first when there are some. */
void request(int act, const char *path)
{
    if (path) snprintf(act_path, sizeof act_path, "%s", path);
    if (is_dirty()) confirm_act = act;
    else run_action(act);
}

void run_action(int act)
{
    confirm_act = ACT_NONE;
    switch (act) {
    case ACT_NEW: new_project(); break;
    case ACT_OPEN: file_dialog(0); break;
    case ACT_OPEN_PATH: open_project(act_path, 0); break;
    case ACT_RECOVER: open_project(autosave_file(), 1); break;
    case ACT_OPEN_EXAMPLE:              /* an untitled copy: Save asks where, so the example stays as it is */
        if (open_project(act_path, 0) == 0) {
            char title[200];
            snprintf(title, sizeof title, "%.190s", base_name(act_path));
            proj_path[0] = 0;
            mark_saved();
            status("Opened the example %s. Press Space to play; Save keeps your own copy.", title);
        }
        break;
    case ACT_QUIT: quit_now = 1; break;
    }
}
