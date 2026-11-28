/* tinydaw: what the interface modules share (types, globals, functions). Made by splitting the old
 * main.c; each module includes this and defines its own part. Not for the engine files. */
#ifndef TINYDAW_UI_H
#define TINYDAW_UI_H
#define UI_UNUSED __attribute__((unused))

#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "daw.h"
#include "theory.h"
#include "midi.h"
#include "scenes.h"
#include "midifile.h"
#include "compat.h"
#include <ctype.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
/* the microphone (see "the microphone", further down) */
#define CAP_RING (1 << 20)                  /* frames: about 20 s at 48 kHz */
typedef struct { unsigned w; long playhead; int playing; } T_cap_marks;
typedef struct {
    int on, track;
    float *buf;
    long n, cap;
    long start;                             
    int start_known, lost;
} T_mic;
/* The audio callback runs on its own thread; every change to the project from the UI
 * happens inside LOCK/UNLOCK so the callback never sees a half-edited clip list. */
#define LOCK()   do { if (dev) SDL_LockAudioDevice(dev); } while (0)
#define UNLOCK() do { if (dev) SDL_UnlockAudioDevice(dev); } while (0)
/* ---------------------------------------------------------------- colours */

typedef struct { Uint8 r, g, b, a; } Col;
enum { THEME_DARK, THEME_LIGHT, THEME_SYSTEM, THEME_XP, THEME_AERO, THEME_BLENDER, THEME_COUNT };
typedef struct {
    int radius, gloss;                  
    Col btn_top, btn_bot, btn_border, hover_top, hover_bot, hover_border;
    Col on_top, on_bot, on_border, on_text, btn_text;
    Col field_bg, field_border;
    int field_radius;
    Col bar_top, bar_bot, bar_text, bar_dim, bar_hi;
    Col rail_top, rail_bot, rail_text, rail_dim, rail_on;
    int bar_gloss, bubbles, shaped;
} T_ST;
/* ---------------------------------------------------------------- text */

typedef struct { char s[128]; Uint32 key; SDL_Texture *t; SDL_Renderer *r; int w, h; unsigned used; } TextEntry;
typedef struct { SDL_Rect r; char s[40]; } T_tlog;
enum { D_NONE, D_LIBPEND, D_LIB, D_MOVE, D_TRIML, D_TRIMR, D_PLAYHEAD, D_LOOP, D_NUM, D_NOTE, D_TVOL, D_BEAT,
       D_FADEIN, D_FADEOUT, D_BAND, D_SCROLL, D_AUTO, D_NMOVE, D_NLEN, D_NBAND, D_VEL, D_SLIDE, D_ED, D_FADER, D_DRUM,
       D_SEC, D_SECNEW, D_SECEND, D_BEND, D_BENDX, D_CHD, D_CHDNEW, D_CHDEND, D_CBEND, D_CBENDX, D_SLOT, D_SCENE, D_PART };
/* Pop-out windows: the clip editor and the mixing console. Each has its own renderer and mouse;
 * a drag belongs to the window it started in (drag_pop, -1 = the main window). */
typedef struct { int mx, my, ldown, lpress, lrel, rpress, wheel_x, wheel_y, ldouble; } PopIn;
typedef struct { SDL_Window *win; SDL_Renderer *ren; Uint32 id; PopIn in; } Pop;
enum { POP_EDITOR, POP_MIXER, NPOPS };
#define ewin (pops[POP_EDITOR].win)
#define eren (pops[POP_EDITOR].ren)
#define ewin_id (pops[POP_EDITOR].id)
#define ein (pops[POP_EDITOR].in)
#define drag_in_editor (drag_pop >= 0)
#define ED_LIB (-2)                     /* ed_clip: the editor has a library sound, as ed_lib */
typedef struct {
    int kind, sample, clip, track, sx, sy;
    long a, b, c, grab;
    double *num, nmin, nmax, nstep, norig;
    int *inum, iorig, imin, imax;
} T_drag;
typedef struct { int top, left, head, ruler, arr, chd, status, lane_h, auto_h, tl_x, tl_w, lanes_y, pane_x, pane_w; } T_L;
/* Theory pane state */
#define PANE_W 480
enum { RI_SOUNDS, RI_THEORY, RI_PADS, RI_DRUMS, RI_MIXER, RI_VISUAL, RI_SETTINGS, RI_COUNT };
enum { COPY_NONE, COPY_SOUND, COPY_CLIPS, COPY_SLOT };
/* ---------------------------------------------------------------- metronome */

/* A click on every beat (or 8th/16th), louder on the first beat of each bar, and an optional
 * count-in before playback starts. It plays in the audio callback on top of everything and is
 * never exported. Its settings belong to this computer (settings.conf), not to a project. */
enum { CLICK_CLICK, CLICK_BEEP, CLICK_WOOD, CLICK_SOUNDS };
typedef struct {
    int on, sound, sub, accent, countin, countin_rec_only;
    float volume;
} T_MET;
#define MET_PI 3.14159265358979323846
typedef struct {
    char *buf, orig[1024];
    size_t cap;
    int path;                           
    SDL_Rect r;
    size_t caret;                       
    int tx;                             
} T_tf;
typedef struct {
    int have, win;
    SDL_Rect r;
    char text[300];
} T_tipc;
typedef struct { int win; SDL_Rect r; Uint32 since; int pressed; } T_tip_seen;
/* ---- drop-down lists */

/* A field showing the current choice with an arrow. The arrow opens a list to pick from; with
 * `cycle`, clicking the rest of the field still steps to the next choice as before. The list is
 * drawn on top of everything at the end of the frame (dd_draw) and takes the next click
 * (dd_input). Long lists split into columns so they never need scrolling. */
#define DD_MAX 160
typedef struct {
    int open, *sel, n, cols, changed_for_set;
    int *changed_for;
    char items[DD_MAX][48];
    SDL_Rect anchor, box;
} T_dd;
typedef struct {
    int active, pending;
    double *v, lo, hi, value;
    char buf[24];
    SDL_Rect r;
    int *iv;                            
    size_t caret;
} T_numedit;
/* ---------------------------------------------------------------- undo and redo */

/* Undo works on snapshots of everything an edit can change. Instead of every edit having to
 * remember to record itself, the state is compared with the last snapshot after each frame
 * (once no drag is in progress): any difference becomes one undo step. A whole drag, a placed
 * beat or a recorded take is therefore a single step. Library sounds are never removed, so
 * sample indices in old snapshots stay valid. */
typedef struct {
    Clip *clips;
    int nclips;
    Note *notes;
    int nnotes;
    AutoPoint *autos;
    int nautos;
    DrumPat *drums;
    int ndrums;
    Section sections[MAX_SECTIONS];
    int nsections;
    SongChord *chords;
    int nchords;
    Bend *bends;
    int nbends;
    char tags[128];
    Bus buses[MAX_BUSES];
    int nbuses;
    Slot *slots;                        /* the session's grid (NULL while it has no scenes) */
    int nscenes;
    char scene_names[MAX_SCENES][24];
    int key_root, key_scale, nkeys;
    KeyChange keys[MAX_KEYS];
    Track tracks[MAX_TRACKS];
    int ntracks;
    double bpm, a4;
    int ts_num, ts_den, nchanges;
    MeterChange changes[MAX_METER];
    long loop_start, loop_end;
    Pad pads[NPADS];
    int inst_sample, inst_root, inst_track;
    MasterFx fx;
    unsigned char removed[MAX_SAMPLES];  /* sounds taken out of the library */
    char label[48];
} State;
#define UNDO_MAX 300
enum { ACT_NONE, ACT_NEW, ACT_OPEN, ACT_OPEN_PATH, ACT_QUIT, ACT_RECOVER, ACT_OPEN_EXAMPLE };
#if defined(_WIN32)
#include <windows.h>
#endif
#if defined(_WIN32)
#include <commdlg.h>
#endif
#if defined(_WIN32)
#include <SDL_syswm.h>
#endif
/* ---------------------------------------------------------------- pads and live input */

/* Sixteen pads play library sounds from the computer keyboard (while the Pads pane is open),
 * game controllers and other USB HID controllers (SDL joysticks), and MIDI devices. Which key,
 * button or note triggers which pad is a property of your hardware, not of a song, so the
 * bindings live in ~/.config/tinydaw/pads.conf; the sounds on the pads are saved in the
 * project. MIDI notes that are not bound to a pad play the "instrument" sound, pitched. */

enum { SRC_KEY, SRC_JOY, SRC_MIDI, SRC_MOUSE };
typedef struct { int key, joy, midi; } Bind;     /* -1 = none */
/* joysticks: SDL instance ids, plus per-axis and per-hat state to turn them into buttons */
#define MAX_JOYS 8
/* A stick can bend pitch like a MIDI pitch wheel: which one (JB_*), and each controller's raw
 * axis numbers for left x, left y, right x, right y (from SDL's controller mapping). */
enum { JB_OFF, JB_LEFT_Y, JB_RIGHT_Y, JB_LEFT_X, JB_RIGHT_X, JB_COUNT };
/* hits waiting to be written onto the timeline (filled under the audio lock, from any thread) */
typedef struct { int pad, note, down, vel; long frame; } RecEv;
/* ---- recording hits onto the timeline */

typedef struct { int on; long start; int sample; int track; int loop; } Held;
typedef struct {
    int on, track, hits, number;
    long start;                         
    Uint32 since;                       
    int note_clip, notes;               
    int held[128];                      
} T_take;
enum { CM_LIB, CM_PAD, CM_INST, CM_SEC, CM_CHD, CM_CHDS, CM_SESS };   /* what a right-click menu is for */
enum { PAD_PRESET_COUNT = 6 };
/* ---- the example songs, from the examples folder next to the program */

#include <dirent.h>
/* ---- recent projects, and the project browser (Ctrl+O): your projects in Music/tinydaw, the ones you
 * opened lately and the examples, each as a card with a picture of its first bars, what it is, and tags */

#include <sys/stat.h>
#define RECENT_MAX 20
typedef struct { char path[1024]; long mtime; int ok; ProjectInfo info; } BrowseItem;
#define NTS 8
/* ---- rearranging the song by whole bars (the arranger's sections)
 *
 * Moving, copying or taking out bars [a, b) moves everything on the timeline with them: clips
 * (split where they cross a boundary), automation, tempo and time signature changes, and sections.
 * Each thing keeps its place inside its bar, so a section keeps its own tempo wherever it goes. */

enum { SE_MOVE, SE_COPY, SE_CUT };          /* move [a,b) to before bar t; copy it in at b; take it out */
typedef struct { int mode, a, b, t; } SongEdit;
enum { CBP_FLAT, CBP_SCOOP, CBP_FALL, CBP_VIB, CBP_RISE, CBP_DIVE, CBP_COUNT };
enum { PK_TRACK, PK_DELAY, PK_REVERB, PK_KINDS };
#define BUILTIN_TRACK 12
#define BUILTIN_DELAY 6
#define BUILTIN_REVERB 5
typedef struct { char names[64][48]; const char *ptr[64 + 16]; int n, nbuilt; Uint32 at; } T_plist;
/* ---------------------------------------------------------------- visualizer */

/* The audio callback copies everything it plays into a ring; each frame the UI takes the newest
 * VIS_FFT frames from it, plus each track's peak level from the mixer. From those come the
 * meters, the analyzer pane and the full-screen show. */
#define VIS_RING 16384
typedef struct { float level, peak; Uint32 peak_at; int clipped; } Meter;
#define VIS_PI 3.14159265358979323846
#define SHOW_FADE 1.6
/* ---------------------------------------------------------------- the rail and panels */

/* A column of category buttons down the left. One panel is open beside it at a time; clicking
 * the open one again hides it and gives the timeline the room. */
#define RAIL_W 70
typedef struct {
    int has, sample, from_pad;
    Pad pad;                            
} T_sclip;
enum { CMA_COPY, CMA_PASTE, CMA_REMOVE, CMA_CLEAR, CMA_PIN, CMA_SAVE, CMA_EDIT,
       CMA_SREN, CMA_SLOOP, CMA_SDUP, CMA_SDEL, CMA_SCUT,
       CMA_CCHG, CMA_CHEAR, CMA_CDUP, CMA_CKEY, CMA_CDEL, CMA_CDETECT, CMA_CPART, CMA_CCLEAR };
typedef struct {
    int open, kind, idx, n;
    int act[10], enabled[10];
    char label[10][80];
    SDL_Rect box;
} T_cm;
/* ---- Create > Voice (the Singer): type words, pick a tune and a voice, and it sings them. The words are
 * spoken by eSpeak NG and sung through a vocoder (voice.c). */

enum { TUNE_ONE, TUNE_UPDOWN, TUNE_RISE, TUNE_FALL, TUNE_BOUNCE, TUNE_ANSWER, TUNE_CLIP, TUNES };
enum { BEND_SCOOP, BEND_SLIDE, BEND_FALL };
typedef struct { long start, length; int note; } T_norig;
/* Each bar and beat line of the tempo map that falls between xmin and xmax, to `fn`: kind 0 a
 * bar (labelled when there's room for its number), 1 a beat of its time signature, 2 a finer
 * grid line (every `sub` quarter notes, 0 for none). Lines thin out when they'd crowd. */
typedef void (*GridFn)(int x, int bar, int kind, int labelled, void *ud);
typedef struct { SDL_Rect area; } TlGrid;
typedef struct { int clip, pattern; } T_pt_last;
typedef struct { int id, src, from, steps, bar; } T_fl_made;
/* what was played, for Capture: a slot on a track over [from, to) of the timeline, its passes counted
 * from `at`, when it was launched (as add_session plays them) */
typedef struct { int track; Slot slot; long from, to, at; } SessSeg;
/* ---- menus */

enum { SMA_PUT = 100, SMA_SECTION, SMA_LONGER, SMA_SHORTER, SMA_CLEAR, SMA_RENAME_S, SMA_LAUNCH_S, SMA_INSERT_S,
       SMA_REMOVE_S, SMA_CAPTURE, SMA_COPY, SMA_PASTE, SMA_IMPORT, SMA_TO_TIMELINE };
/* the transport's icons, drawn so they look the same in every theme and font */
enum { TI_START, TI_BACK, TI_PLAY, TI_PAUSE, TI_STOP, TI_FWD, TI_END, TI_LOOP, TI_REC };
/* ---------------------------------------------------------------- dragging */

#define MIN_CLIP (SR / 100)
/* ---------------------------------------------------------------- theory pane */

#define PI_D 3.14159265358979323846

/* ---- globals */
extern Project P;
extern SDL_AudioDeviceID dev;
extern int out_buffer;
extern SDL_AudioDeviceID cap_dev;
extern int cap_channels, cap_buffer, cap_fake;
extern char audio_in[256];
extern float in_gain_db;
extern int monitor_on;
extern double rec_offset_ms;
extern float *cap_ring;
extern SDL_atomic_t cap_w;
extern unsigned cap_r, mon_r;
extern float in_peak, in_meter;
extern unsigned char track_arm[MAX_TRACKS];
extern T_cap_marks cap_marks[64];
extern SDL_atomic_t cap_nmark;
extern T_mic mic;
extern SDL_Window *win;
extern SDL_Renderer *ren;
extern TTF_Font *font;
extern int font_h;
extern int W, H;
extern SDL_Cursor *cur_arrow, *cur_resize, *cur_hand;
extern Col BG, PANEL, PANEL2, PANEL3, LINE, TEXT, DIM, ACCENT, DARK, HEAD;
extern int theme_light;
extern T_ST ST;
extern int theme_kind;
extern int theme_choice;
extern const Col TRACK_COL[MAX_TRACKS];
extern TextEntry tcache[512];
extern unsigned tclock;
extern float ui_user;
extern int ui_choice;
extern float ui_px;
extern int font_pt;
extern int tcheck, ntlog;
extern T_tlog tlog[4096];
extern char status_msg[300];
extern int mx, my, ldown, lpress, lrel, rpress, wheel_x, wheel_y;
extern SDL_Keymod mods;
extern Pop pops[NPOPS];
extern int drag_pop;
extern SDL_Rect lv_fader[MAX_TRACKS], cs_fader[MAX_TRACKS], cs_mute[MAX_TRACKS];
extern int ed_clip, ed_handle;
extern Clip ed_last, ed_orig;
extern Clip ed_lib;
extern SDL_Rect ed_layer_r[4], ed_rr_r[3];
extern int ed_nclips;
extern long ed_v0, ed_v1;
extern SDL_Rect ed_wave, ed_btn[8];
extern T_drag drag;
extern T_L L;
extern int auto_open[MAX_TRACKS], auto_param[MAX_TRACKS];
extern int roll_clip, roll_pattern, roll_top, roll_kh;
extern double roll_saved_view, roll_saved_pps;
extern long roll_len;
extern unsigned char nsel[MAX_NOTES];
extern int ldouble;
extern int pads_sub, mixer_sub, vis_sub, settings_sub, sounds_sub;
extern int pads_open;
extern int drums_open;
extern int drum_cur;
extern int drum_mode;
extern int drums_scroll;
extern SDL_Rect dr_cell[DRUM_ROWS][DRUM_STEPS], dr_head[DRUM_ROWS], dr_add;
extern int sec_renaming;
extern int mixer_open;
extern int vis_open;
extern int show_open;
extern int show_scene, show_cycle;
extern double show_each, show_clock, show_started[SCENE_COUNT + 1];
extern int sounds_open;
extern int lib_badge;
extern Uint32 lib_badge_at;
extern int th_len;
extern int settings_open;
extern Mixer *MX;
extern int sel_track;
extern int headless;
extern int theory_open, theory_tab, th_root, th_scale, th_sevenths, th_scroll, th_content_h;
extern int chd_sel;
extern int chp_open, chp_idx;
extern float *scratch;
extern long scratch_len, scratch_pos;
extern double view_start;
extern double pps;
extern int selected;
extern unsigned char csel[MAX_CLIPS];
extern int lib_scroll;
extern Loud live_loud;
extern double tempo_from;
extern Uint32 tempo_at;
extern SDL_Rect lib_rects[MAX_SAMPLES];
extern GenParams G;
extern int snap_i;
extern int ui_offscreen;
extern Clip clipboard[MAX_CLIPS];
extern int nclipboard;
extern int last_copy;
extern long g_start[MAX_CLIPS];
extern unsigned char band_base[MAX_CLIPS];
extern int g_track[MAX_CLIPS];
extern T_MET MET;
extern const char *const CLICK_NAMES[CLICK_SOUNDS];
extern float *click_buf[3];
extern long click_len, click_pos;
extern int click_kind, click_rate, click_sound;
extern long countin_left, countin_total;
extern int pads_rec;
extern long play_from;
extern Uint32 taps[8];
extern int ntaps;
extern ExportOptions EXO;
extern int export_open, export_pending;
extern char export_dir[900];
extern char export_name[256];
extern char export_base_path[1200];
extern SDL_Rect export_dir_r, export_name_r, export_go_r;
extern char proj_path[1024];
extern T_tf tf;
extern int tf_drawn;
extern int tips_on;
extern T_tipc tipc;
extern T_tip_seen tip_seen;
extern char tip_shown[300];
extern T_dd dd;
extern T_numedit numedit;
extern State undo_stack[UNDO_MAX], redo_stack[UNDO_MAX];
extern int nundo, nredo;
extern State committed;
extern State saved;
extern int saved_nsamples, edit_serial;
extern int th_synced_root, th_synced_scale;
extern int recovered;
extern int autosaved_serial;
extern Uint32 last_autosave;
extern int confirm_act;
extern int after_save_act;
extern char act_path[1024];
extern int menu_open, quit_now;
extern SDL_atomic_t fd_state;
extern int fd_saving;
extern int fd_export;
extern char fd_result[1024];
#if !defined(_WIN32)
extern char fd_cmd[4000];
#endif
#if defined(_WIN32)
extern char fd_start[2100];
#endif
#if defined(_WIN32)
extern HWND fd_owner;
#endif
extern int slot_import_t, slot_import_s;
extern Bind binds[NPADS];
extern int learn_pad, learned, binds_changed;
extern int inst_midi;
extern Uint32 pad_flash[NPADS], key_flash, joy_flash[8], midi_flash[MIDI_MAX_DEVICES];
extern int midi_ok;
extern int mouse_pad;
extern SDL_Joystick *joys[MAX_JOYS];
extern int njoys, joy_gc[MAX_JOYS];
extern Sint16 axis_rest[MAX_JOYS][16];
extern Uint8 axis_on[MAX_JOYS][16], hat_bits[MAX_JOYS][4];
extern const char *const JB_NAMES[JB_COUNT];
extern int joy_bend;
extern int stick_axis[MAX_JOYS][4];
extern SDL_GameController *joy_ctl[MAX_JOYS];
extern RecEv recq[512];
extern int nrecq;
extern unsigned live_rr;
extern Held held_pad[NPADS];
extern T_take take;
extern int rec_quantize;
extern int pad_sel, pads_scroll, pads_content_h;
extern SDL_Rect pad_rects[NPADS], inst_drop;
extern const char *MODE_NAMES[PAD_MODES];
extern const char *const PAD_PRESETS[PAD_PRESET_COUNT];
extern int gn_sel;
extern SDL_Rect file_btn, met_btn, snap_rect, bpm_rect, a4_rect, ts_rect, change_flag[MAX_METER];
extern int met_open;
extern char example_path[16][1024], example_title[16][96];
extern int nexamples, example_pick;
extern char recent[RECENT_MAX][1024];
extern int nrecent, recent_read, recent_pick, recent_map_[RECENT_MAX];
extern BrowseItem *bcache;
extern int nbcache, capbcache;
extern int bitems[512], nbitems;
extern int browse_open, browse_tab, browse_scroll, browse_content_h, browse_editing;
extern char browse_q[64], browse_tag[48], browse_tags_buf[128];
extern SDL_Rect browse_card_r[512];
extern int mixer_scroll, mixer_content_h;
extern SDL_Rect cb_box;
extern double cb_last;
extern const char *const PK_DIR[PK_KINDS];
extern const char *const TRACK_PRESET_NAMES[BUILTIN_TRACK];
extern const char *const DELAY_PRESET_NAMES[BUILTIN_DELAY];
extern const char *const REVERB_PRESET_NAMES[BUILTIN_REVERB];
extern T_plist plist[PK_KINDS];
extern int macro_open;
extern float vis_ring[2 * VIS_RING];
extern int vis_pos;
extern float vis_buf[2 * VIS_FFT], vis_mono[VIS_FFT], vis_db[VIS_FFT / 2], vis_smooth[VIS_FFT / 2];
extern Meter track_meter[MAX_TRACKS], master_meter[2], bus_meter[MAX_BUSES];
extern float low_energy, low_avg, high_energy, beat_pulse;
extern Uint32 vis_last;
extern int vis_scroll, vis_content_h;
extern SDL_Texture *show_tex;
extern int show_w, show_h;
extern Uint32 *show_px;
extern float blob_size[MAX_TRACKS], show_phase, ripple_t[6];
extern int ripple_n;
extern double last_beat;
extern SDL_Texture *scene_tex;
extern int scene_tw, scene_th;
extern float *scene_buf, *scene_buf2;
extern size_t scene_cap;
extern Uint32 *scene_px;
extern double show_switch_at, show_wall;
extern int show_fade_from;
extern double show_fade_left;
extern Uint32 show_last_tick;
extern int *const RAIL_FLAG[RI_COUNT];
extern const char *const RAIL_NAME[RI_COUNT];
extern int last_panel;
extern int sounds_scroll, sounds_content_h;
extern T_sclip sclip;
extern T_cm cm;
extern int lib_flash;
extern Uint32 lib_flash_at;
extern int saved_enabled;
extern char pins[256][200];
extern int npins, pins_read;
extern float *heard_buf;
extern SDL_Rect ns_note_r;
extern int gx_genre, gx_kind, gx_preset;
extern SynthSound GX;
extern float *gx_heard;
extern double *slide_done;
extern int gx_reveal;
extern SDL_Rect gx_row_r[128], gx_genre_r[16], gx_kind_r[12], gx_slide_r[MACROS], gx_lib_r, gx_place_r, gx_inst_r;
extern int gx_nvel, gx_nrr;
extern const char *const TUNE_NAMES[TUNES];
extern const int TUNE_DEGREES[TUNE_CLIP][8];
extern VocalSpec SV;
extern int sv_init, sv_espeak;
extern float *sv_heard;
extern char sv_err[200];
extern SDL_Rect sv_words_r, sv_tune_r[TUNES], sv_sing_r, sv_lib_r, sv_place_r, sv_voice_r[2], sv_style_r[VS_STYLES], sv_singer_r[8], sv_bend_r[4];
extern SDL_Rect sv_curve_r;
extern double sv_last_beat;
extern char audio_out[256];
extern int settings_scroll, settings_content_h;
extern SDL_atomic_t theme_probe;
extern SDL_Rect roll_grid, roll_keys, roll_vel, roll_ruler;
extern int roll_voice;
extern T_norig norig[MAX_NOTES];
extern unsigned char nband_base[MAX_NOTES];
extern int session_view;
extern SDL_Rect sec_rect_[MAX_SECTIONS];
extern SDL_Rect chd_rect_[MAX_CHORDS], chp_box;
extern double chd_at0, chd_menu_beat;
extern int pt_kind;
extern SDL_Rect pt_handle;
extern int pt_style[PART_KINDS];
extern double pt_busy[PART_KINDS];
extern int pt_octave[PART_KINDS];
extern int pt_sound[PART_KINDS];
extern int pt_where;
extern unsigned pt_seed[PART_KINDS];
extern T_pt_last pt_last[PART_KINDS];
extern int pt_pre[PART_KINDS][64], pt_npre[PART_KINDS];
extern const char *pt_names[PART_KINDS][65];
extern const int PT_LO[PART_KINDS], PT_HI[PART_KINDS];
extern const char *const PT_TRACK[PART_KINDS];
extern int fl_style, fl_where, fl_track;
extern int pt_force_preset, pt_force_track;
extern double pt_from, pt_to;
extern double fl_busy;
extern unsigned fl_seed;
extern T_fl_made fl_made[MAX_DRUMS];
extern int fl_n;
extern int sess_quant;
extern int sess_scroll;
extern int sess_renaming;
extern SDL_Rect sess_cell_[MAX_TRACKS][MAX_SCENES], sess_scene_r[MAX_SCENES], sess_stop_r[MAX_TRACKS], sess_back_r[MAX_TRACKS];
extern int sess_menu_t, sess_menu_s;
extern SessSeg sess_log[2048];
extern int sess_nlog, sess_open_seg[MAX_TRACKS], sess_seen[MAX_TRACKS];
extern long sess_last_head;
extern const char *const LAUNCH_NAMES[LAUNCH_MODES];
extern Slot slot_board;
extern int slot_board_has;
extern SDL_Rect transport_rect;
extern int gn_bars, gn_split;
extern char gn_pat[32][DRUM_LANES][33];
extern SDL_Rect gn_handle, gn_cell[DRUM_LANES][32];
extern int failures;
extern const double TH_LENS[4];
extern const char *const TH_LEN_NAMES[4];
extern int gn_init;
extern int dr_paint;
extern int drums_content_h, drum_bars, drum_style;
extern const char *RATE_LABELS[6];
extern int console_scroll;

/* ---- functions */
Col rgb(int r, int g, int b);
void theme_apply(int kind);
Col shade(Col c, float k);
Col alpha(Col c, Uint8 a);
Col tone(Col c, float k);
Col wash(Col c, float k);
void fill(SDL_Rect r, Col c);
void outline(SDL_Rect r, Col c);
void line(int x0, int y0, int x1, int y1, Col c);
Col mix_col(Col a, Col b, float t);
int corner_inset(int radius, int yy, int h);
void shape_fill(SDL_Rect r, Col top, Col bot, int radius, int gloss);
void shape_outline(SDL_Rect r, Col c, int radius);
void style_box(SDL_Rect r, int state);
Col btn_fg(int on);
void field_box(SDL_Rect r, int hov, int active);
TextEntry *text_get(const char *s, Col c);
void text_forget(SDL_Renderer *r);
void tcheck_cover(SDL_Rect r);
void tcheck_report(const char *where);
int text(int x, int y, const char *s, Col c);
int text_w(const char *s);
void text_clip(SDL_Rect r, int x, int y, const char *s, Col c);
void text_center(SDL_Rect r, const char *s, Col c);
void status(const char *fmt, ...);
int hit(SDL_Rect r);
int pop_of(Uint32 id);
void tempo_touch(double before);
float window_density(SDL_Window *w);
float ui_auto(void);
float ui_max(void);
void logical_size(SDL_Renderer *r, int *w, int *h);
void ui_apply(void);
void ui_fit_window(void);
void ui_set(int percent);
void mouse_logical(int *x, int *y);
void layout(void);
int track_y(int t);
int lanes_h(void);
SDL_Rect auto_rect(int t);
int auto_lane_at(int y);
double frame_to_x(long f);
double x_to_frame_raw(int x);
long x_to_frame(int x);
int lane_at(int y);
long beat_at(long f);
long beat_frames(void);
long bar_floor(long f);
long bar_ceil(long f);
long bar_round(long f);
long bar_len_at(long f);
long grid_snap(long f, double g);
double snap_beats(void);
void pos_text(long f, char *buf, size_t n);
long meter_step(long f, int dir, int beat);
long snap_frames(void);
long snap(long f);
double quant_grid(void);
int notes_quantize(int pattern, long origin, const unsigned char *mask, double grid, double strength);
int notes_humanize(int pattern, const unsigned char *mask, double ms);
SDL_Rect clip_rect(const Clip *c);
int fade_handles(const Clip *c, SDL_Rect *in, SDL_Rect *out);
int fade_hit(int i);
SDL_Rect volume_bar(int t);
int clip_add(int sample, int track, long start, long offset, long length);
void sel_clear(void);
void sel_only(int i);
int sel_count(void);
void sel_fix_primary(void);
void clip_delete(int i);
int delete_selection(void);
int duplicate_selection(long shift);
void selection_span(long *a, long *b);
void copy_selection(void);
int paste_at(long at);
void group_remember(void);
int over_library(void);
void group_place(long df, int dt);
void preview(int sample);
void metronome_prepare(void);
long next_click(long pos, int countin, int *kind);
void metronome_mix_ex(float *out, long start, int n, int force, int countin);
void metronome_mix(float *out, long start, int n, int force);
void toggle_play(void);
void tap_tempo(void);
void view_show(long f);
void playhead_to(long f);
void playhead_to_edge(int dir);
void zoom_to_fit(void);
void transport_stop(void);
void toggle_loop(void);
int import_file(const char *path);
void export_take_ext(char *name);
void export_make_base(void);
void open_export(void);
void do_export_now(void);
size_t ed_prev(const char *s, size_t i);
size_t ed_next(const char *s, size_t i);
int ed_wordch(char c);
int ed_key(char *buf, size_t *caret, SDL_Keysym k);
void ed_insert(char *buf, size_t cap, size_t *caret, char ch);
int prefix_w(const char *s, size_t i);
size_t ed_caret_at(const char *s, int x0, int x);
void tf_end(int keep);
void tf_insert(const char *t);
int tf_key(SDL_Keysym k);
void textfield(SDL_Rect r, char *buf, size_t cap, int path, const char *placeholder);
void zoom(double factor, int anchor_x);
int button2(SDL_Rect r, const char *label, int on, int enabled);
int button(SDL_Rect r, const char *label, int on);
void db_text(float v, char *buf, size_t n);
void tip(SDL_Rect r, const char *fmt, ...);
void tip_draw(int win_k);
int dropdown(SDL_Rect r, const char *prefix, const char *const *items, int n, int *sel, int cycle);
void dd_layout(void);
SDL_Rect dd_item(int i);
void dd_input(void);
void dd_draw(void);
int track_dropdown(SDL_Rect r, const char *prefix, int *track);
int subtabs(int x, int y, int w, const char *const *names, int n, int *sel);
void scrollbar(SDL_Rect area, int *scroll, int content_h);
void number_box(SDL_Rect r, const char *label, const char *value, int active);
void numedit_begin(double *v, double lo, double hi);
int parse_note(const char *s);
void numedit_end(int commit);
void numedit_text(const char *t);
int numedit_key(SDL_Keysym k);
void numedit_draw(SDL_Rect r, const char *label);
int numedit_widget(SDL_Rect r, const char *label, double *v);
void number_text(SDL_Rect r, const char *label, double *v, double lo, double hi, double step,
                        const char *shown);
void pan_text(float pan, char *buf, size_t n);
double *fproxy(float *f);
void fcommit(float *f, const double *d);
void fnumber(SDL_Rect r, const char *label, float *f, double lo, double hi, double step, const char *fmt);
void number(SDL_Rect r, const char *label, double *v, double lo, double hi, double step,
                   const char *fmt);
void note_number(SDL_Rect r, const char *label, int *v);
void state_free(State *s);
void *dup_mem(const void *src, size_t n);
void state_capture(State *s);
int removed_same(const State *s);
int state_same(const State *s);
void state_restore(const State *s);
void describe(const State *s, char *buf, size_t n);
void stack_push(State *stack, int *n, State s);
void history_check(void);
void bends_update(void);
void history_reset(void);
void undo_redo(int redo);
void state_scale(State *s, double ratio);
void history_scale(double ratio);
void mark_saved(void);
int is_dirty(void);
const char *base_name(const char *path);
const char *dirname_of(const char *path);
void ensure_dir(const char *path);
void projects_dir(char *buf, size_t n);
const char *autosave_file(void);
int autosave_exists(void);
void update_title(void);
int save_to(const char *path);
void replace_project(Project *np);
int open_project(const char *path, int as_recovery);
void new_project(void);
void autosave_tick(void);
#if defined(_WIN32)
void win_file_dialog(void);
#endif
#if !(defined(_WIN32))
int have_program(const char *name);
#endif
#if !(defined(_WIN32))
void shell_quote(char *out, size_t n, const char *s);
#endif
int fd_thread(void *arg);
void file_dialog(int saving);
void export_file_dialog(void);
void midi_file_dialog(int save);
void export_take_path(const char *path);
void slot_import_dialog(int t, int s);
void file_dialog_poll(void);
void save_project(int as);
void request(int act, const char *path);
void run_action(int act);
void binds_default(void);
const char *binds_file(void);
void binds_load(void);
void binds_save(void);
void joy_label(int code, char *buf, size_t n);
void midi_label(int code, char *buf, size_t n);
void bind_label(int pad, char *buf, size_t n);
void rec_push_vel(int pad, int note, int down, int vel);
void rec_push(int pad, int note, int down);
void pad_hit(int pad, int vel, int down);
void inst_note(int note, int vel, int down);
void input_event(int src, int code, int vel, int down, int device);
void midi_in(int code, int velocity, int device, void *ud);
int joy_index(SDL_JoystickID id);
void joy_added(int device_index);
void joy_removed(SDL_JoystickID id);
void joy_event(const SDL_Event *e);
void rec_close(Held *h, long end);
void poly_fill(const float *xy, int n, Col c);
void disc_small(int cx, int cy, int r, Col c);
int take_track_for(int from_track);
int take_note_clip(long at);
void take_note_fit(void);
void take_note_on(int note, int vel, long frame);
void take_note_off(int note, long frame);
void take_notes_finish(void);
void take_check(void);
void rec_toggle(void);
void rec_timer(SDL_Rect r);
void rec_drain(void);
void pad_assign(int pad, int sample);
int pads_drop(int sample);
void track_picker(int x, int y, const char *label, int *track);
void device_row(int x, int y, int w, const char *name, Uint32 flash, Col c);
int devices_section(int x, int y, int w);
int pads_content(int x, int y, int w);
void pads_pane(void);
int pads_take_key(SDL_Keysym k);
int menu_item(SDL_Rect r, const char *label, const char *shortcut, int enabled);
void examples_scan(void);
const char *recent_file(void);
void recent_load(void);
void recent_add(const char *path);
long file_mtime(const char *path);
int browse_item(const char *path);
void browse_walk(const char *dir, int depth);
int browse_cmp_mtime(const void *a, const void *b);
void browse_projects_dir(char *buf, size_t n);
void browse_scan(void);
void browse_show(void);
void browse_name(const BrowseItem *b, char *buf, size_t n);
int has_tag(const char *tags, const char *tag);
int browse_matches(const BrowseItem *b);
void browse_thumb(SDL_Rect r, const ProjectInfo *in);
void browse_when(long mt, char *buf, size_t n);
void browse_open_item(int k);
void browse_tags_commit(int k);
void browse_draw(void);
void recent_menu(SDL_Rect anchor);
void examples_menu(SDL_Rect anchor);
void file_menu(void);
int change_at(long f);
int change_add_here(void);
void change_remove(int k);
double se_map(const SongEdit *e, double x, int *gone);
void se_split_at(long f);
void se_where(long f, int *bar, double *into);
long se_frame(int bar, double into);
void song_edit(SongEdit e);
void metronome_menu(void);
void confirm_dialog(void);
void waiting_overlay(const char *msg);
void clip_variant(int ci, int semis, int reverse);
void clip_variant_c(Clip *c, int semis, int reverse);
int clip_semis(const Clip *c);
int clip_rev(const Clip *c);
int track_has_fx(const Track *t);
int section(int x, int y, int w, const char *title);
Bend *cb_edit(void);
void cb_erase(Bend *b, double a, double z);
void cb_put(Bend *b, double at, double semis);
void cb_preset(int kind);
int clip_bend_editor(int x, int y, int w);
void preset_dir(int kind, char *buf, size_t n);
void macro_set(Macro *m, const char *name, float value);
void macro_link(Macro *m, int param, float lo, float hi);
void builtin_track_preset(int i, Track *t);
void builtin_fx_preset(int kind, int i, MasterFx *fx);
int preset_write(int kind, const char *name, const Track *t, const MasterFx *fx);
int preset_read(int kind, const char *name, Track *t, MasterFx *fx);
void preset_list(int kind);
void preset_apply(int kind, int i, Track *t);
int preset_row(int x, int y, int w, int kind, Track *t);
int macros_section(int x, int y, int w, Track *t);
int mixer_content(int x, int y, int w);
extern int sel_bus;
int macro_choices(const Track *t, int *ids, const char **names, int max);
int macro_linked(const Track *t, int tp);
int rack_editor(int x, int y, int w, Rack *rk, int key);
int output_section(int x, int y, int w, Track *t);
int bus_add(const char *name);
int buses_tab(int x, int y, int w);
void mixer_pane(void);
float to_db(float v);
void meter_feed(Meter *m, float v, float dt);
int loudness_readout(int x, int y, int w);
void vis_update(void);
Col meter_col(float db);
void meter_draw(SDL_Rect r, const Meter *m, int vertical);
double freq_x(double f, SDL_Rect r);
void draw_spectrum(SDL_Rect r);
void draw_scope(SDL_Rect r);
void draw_stereo(SDL_Rect r);
int vis_content(int x, int y, int w);
void vis_pane(void);
void show_blobs(void);
void show_text(int x, int y, const char *s, Col c);
void show_overlay(const char *name, const char *caption);
const char *show_name(int k);
void show_go(int k);
void show_draw(void);
void show_toggle(void);
void open_panel(int which);
void show_panel(int which);
void rail_icon(int kind, int cx, int cy, Col c);
void rail(void);
SDL_Rect panel_rect(void);
int panel_begin(SDL_Rect pane, int *scroll, int content_h, int *saved_press);
void panel_end(SDL_Rect pane, int *scroll, int content_h, int inside, int saved_press);
void remove_sound(int i);
void sound_copy(int kind, int idx);
int sclip_library_sound(void);
void sound_paste(int kind, int idx);
void saved_dir(char *buf, size_t n);
int in_saved_dir(const char *path);
void pins_load(void);
void pins_save(void);
int pin_find(const char *file);
void pin_set(const char *file, int on);
int saved_sync(void);
int sound_save(int i);
void sound_pin(int i, int on);
void sound_unsave(int i);
int sound_under_mouse(int *kind, int *idx);
void cm_open(int kind, int idx);
void cm_open_section(int i);
void sec_action(int act, int i);
void cm_draw(void);
void library_list(int x, int y, int w, int bottom);
int new_sound(int x, int y, int w);
int slider(SDL_Rect r, double *v);
void gx_play(void);
void gx_pick(int preset);
int gx_matches(int i);
void gx_name(char *buf, size_t n);
int gx_keep(void);
int free_track_at(long at, long len);
void gx_surprise(void);
int gx_row(int x, int y, int w, int i);
int gx_shape(int x, int y, int w);
int generator(int x, int y, int w);
void sv_defaults(void);
int key_note(int first, int deg);
int sv_clip(void);
int sv_make_notes(void);
void sv_name(char *buf, size_t n);
int sv_keep(void);
void sv_sing(void);
void sv_bend_erase(double a, double b);
void sv_bend_put(double beat, double semis);
int sv_note_at(double beat, int n, int nearest);
void sv_bend_preset(int kind, int n);
int sv_curve(int x, int y, int w);
int singer(int x, int y, int w);
void sounds_pane(void);
int settings_content(int x, int y, int w);
void settings_pane(void);
const char *settings_file(void);
void settings_save(void);
void settings_load(void);
int ask_desktop_theme(void);
int theme_thread(void *arg);
void theme_follow(void);
void theme_poll(void);
void top_bar(void);
float auto_y_to_v(SDL_Rect r, int y);
int auto_v_to_y(SDL_Rect r, float v);
void auto_text(int param, float v, char *buf, size_t n);
float track_base(int t, int param);
void auto_status(int i);
int auto_add(int t, int param, long at, float v);
void auto_delete(int i);
void auto_clear(int t, int param);
int auto_point_at(int t, int param);
void auto_header(int t, int y);
void auto_lane_draw(int t);
int black_key(int note);
void draw_clip_drums(const Clip *c, SDL_Rect r, int body_y, int body_h, Col col);
void draw_clip_notes(const Clip *c, SDL_Rect r, int body_y, int body_h, Col col);
int note_add(int pattern, int note, long start, long len, float vel);
void note_delete(int i);
void roll_open(int c);
void roll_quiet(void);
void roll_close(void);
void roll_hear(int note);
void roll_check(void);
long roll_origin(void);
SDL_Rect note_rect(int i);
int roll_note_at(void);
int roll_row_note(int y);
int nsel_count(void);
void nsel_only(int i);
void roll_fit_clip(void);
int roll_delete_selected(void);
void roll_duplicate_selected(void);
void roll_make_unique(void);
int open_track_from(long at, int *fresh);
void meter_from_file(const MfFile *f);
void midi_import(const char *path);
void midi_export(const char *path);
int new_note_clip(int t, long at);
void roll_drag(void);
void roll_drag_end(void);
int roll_key(SDL_Keysym k);
void grid_walk(int xmin, int xmax, double sub, GridFn fn, void *ud);
void tl_grid_fn(int x, int bar, int kind, int labelled, void *ud);
void roll_grid_fn(int x, int bar, int kind, int labelled, void *ud);
void piano_roll(void);
void track_headers(void);
void fmt_clock(double secs, int fine, char *buf, size_t n);
void time_scale(SDL_Rect r);
Col sec_col(const char *name);
int sec_at_bar(int bar);
int sec_room(int bar);
void sec_sort(void);
int sec_add(int bar, int bars, const char *name);
void sec_add_here(void);
void sec_jump(int dir);
int sec_mouse_bar(int round_it);
int sec_target(int moving);
void arranger_strip(void);
void arranger_release(void);
double chd_beat_at_x(int x);
double chd_bar_beats(double beat);
double chd_bar_start(double beat);
int chd_bar_of(double beat);
double chd_next_after(double beat);
void chd_hear(const SongChord *c);
void chd_where(const SongChord *c, char *buf, size_t n);
void chd_suggest(double at, int *root, int *type);
int chd_add(double at, double len);
void chd_add_here(void);
void open_key_tab(void);
void key_sync(void);
void key_name(int root, int scale, char *buf, size_t n);
void chd_key_change(double beat);
void chord_header(void);
void cm_open_list(int kind, int idx, const char *const *labels, const int *acts, const int *enabled, int n);
void chord_strip(void);
void chord_release(void);
void chord_picker(void);
int chord_part(void);
int chords_from_clip(int ci);
void chord_action(int act, int i);
void pt_init(void);
int pt_range(double *a, double *b);
int pt_sample(int kind, double longest);
int pt_free_track(long a, long b, const char *name);
int part_make(int kind, int again);
int part_drop_range(int kind, long frame, double *a, double *b);
int part_drop(int kind, long frame, int track);
int fl_default_track(void);
int clip_split_one(int c, long f);
void fl_rewrite(int i);
int fills_make(int again);
int parts_tab(int x, int y, int w);
long sess_boundary(void);
void sess_queue(int t, int what);
void sess_launch(int t, int s);
void sess_release_now(int t);
void sess_launch_scene(int s);
void sess_all(int what);
int sess_add_scene(const char *name);
void sess_remove_scene(int s);
int sess_from_sections(void);
void slot_fill_clip(int t, int s, const Clip *c, const char *name);
void sess_drop_sound(int sample);
int slot_clips(int t, const Slot *sl, Clip *out, int max);
void slot_copy(int t, int s);
int slot_can_paste(void);
void slot_paste(int t, int s);
void slot_clear(int t, int s);
void slot_to_timeline(int t, int s);
int slot_under_mouse(int *t, int *s);
void slot_import_path(int t, int s, const char *path);
int slot_drop_file(const char *path);
void sess_close(int t, long to);
void sess_track(void);
int sess_capture(void);
void sess_forget(void);
void sess_rename_scene(int s);
void sess_action(int act);
void slot_release(void);
void session_lane_marks(void);
void session_header(void);
void session_grid(void);
void timeline(void);
void tri(int x, int cy, int hgt, int dir, Col c);
void transport_icon(int kind, SDL_Rect r, Col c);
int transport_button(SDL_Rect r, int kind, int on, Col tint);
void status_bar(void);
void update_drag(void);
void bounce_to_library(void);
void bounce_clips(const int *list, int n);
void end_drag(void);
void tempo_apply(double from);
void tempo_check(void);
void key(SDL_Keysym k);
void audio_cb(void *ud, Uint8 *stream, int len);
void open_audio(void);
int armed_track(void);
void cap_cb(void *ud, Uint8 *stream, int len);
void close_input(void);
int open_input(void);
double rec_offset_auto(void);
double rec_offset(void);
void input_update(void);
void mic_take_start(void);
int mic_take_end(void);
void set_rate(int rate);
int gen(int wave, int note, double beats, double vol, double att, double rel);
void make_demo(void);
void sim(int x, int y, int press, int release, int right, SDL_Keymod m);
void sim_drag(int x0, int y0, int x1, int y1, SDL_Keymod m);
void ed_sim(int x, int y, int press, int release);
void check(int ok, const char *what);
int lane_y(int t);
int dropdown_probe_snap(int x);
int snap_arrow_x(void);
float *test_render(Project *p, long a, long n, int only);
double test_rms(const float *b, long a, long e, int ch);
int selftest(void);
TTF_Font *open_font(int size);
void play_scratch(float *buf, long frames);
GenParams chord_params(double beats);
void preview_notes(const int *notes, int n, double beats);
int chord_sample(const int *notes, int n, const char *label, double beats);
void library_added(int s, int existed);
int add_to_library(const int *notes, int n, const char *label);
long roll_insert_at(void);
int roll_put_chords(const int (*notes)[6], const int *counts, int nch, double beats, const char *what);
int roll_put_chord(const int *notes, int n, double beats, const char *name);
int roll_button(SDL_Rect r);
int plus_button(SDL_Rect r);
int adds_as(int x, int y, int w);
int prog_flats(const Progression *pr);
void play_progression(const Progression *pr);
void place_progression(const Progression *pr);
int chord_button(SDL_Rect r, const char *top, const char *bottom);
int row_hit(SDL_Rect r);
void key_signature_text(int parent, int flats, char *buf, size_t n);
int tab_keys(int x, int y, int w);
void disc(int cx, int cy, int r, Col c);
void ring(int cx, int cy, int r, Col c);
int tab_circle(int x, int y, int w);
int tab_chords(int x, int y, int w);
int tab_intervals(int x, int y, int w);
int tab_tempo(int x, int y, int w);
int tab_notes(int x, int y, int w);
void genre_init(void);
double kick_tuned_hz(void);
double genre_bpm(int g);
BeatSpec genre_spec(int g);
int lane_used(const BeatSpec *b, int l);
int beat_parts(const BeatSpec *b, int *masks, const char **labels);
int beat_layout(int g, long *pattern_frames, int *reps);
int same_beat(const BeatSpec *a, const BeatSpec *b);
int beat_sample(const BeatSpec *b, int mask, const char *name);
int hit_sample(const BeatSpec *b, int lane, const char *name);
void drums_show(int id);
DrumPat *drum_new(int track, const char *name);
DrumPat *drum_from_style(int track, int g);
int drum_clip_sound(const DrumPat *d);
int drum_place(DrumPat *d, int bars);
void drum_delete(int id);
void drum_audition(int smp);
int drums_drop(int smp);
int drum_now(const DrumPat *d);
int drums_content(int x, int y, int w);
void drums_pane(void);
void set_pad(int k, int sample, int mode, float vol, int pitch, int track);
void preset_drums(int first, int count);
void preset_chords(int first, int count, int track);
void preset_notes(int base, int track, const char *tag);
void preset_loops(void);
void load_pad_preset(int k);
int track_empty(int t);
void place_beat(int g, long start, int first_track);
void add_beat_to_library(int g);
void play_beat(int g);
int wrapped(int x, int y, int w, const char *s, Col c);
int tab_genres(int x, int y, int w);
SDL_Rect tab_rect(int pos, int x, int y, int w);
void theory_pane(void);
void export_dialog(void);
void pop_close(int k);
int pop_open(int k, const char *title, int w, int h, int minw, int minh);
void pop_pass(int k, void (*draw)(void));
void editor_close(void);
Clip *ed_c(void);
void editor_view_clip(void);
void editor_open(int c);
void editor_open_sound(int s);
int editor_find(void);
long ed_x_to_frame(int x);
int ed_frame_to_x(long f);
void editor_drag(void);
void editor_play_region(long from);
void editor_draw(void);
float *fader_target(int t);
float fader(SDL_Rect r, int t, int vertical);
void mute_solo(SDL_Rect m, SDL_Rect so, int t);
void pan_box(SDL_Rect r, const char *label, int t);
int levels_tab(int x, int y, int w);
void mix_strip(SDL_Rect r, int t);
void master_strip(SDL_Rect r);
void bus_strip(SDL_Rect r, int b);
void console_draw(void);
void mixwin_open(void);
void mixwin_frame(void);
void editor_post(void);
void editor_frame(void);
void frame(void);
int cli_track(const char *name);
int cli_spec(const char *spec, int fills);

/* ---- small helpers and tables every module uses */
static UI_UNUSED const char *SNAP_NAMES[] = {"off", "bar", "1/2", "1/4", "1/8", "1/16"};
static UI_UNUSED const double SNAP_BEATS[] = {0, 4, 2, 1, 0.5, 0.25};
/* ---- editing the tempo map: the section the playhead is in */

static UI_UNUSED const char *const TS_NAMES[] = {"2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "9/8", "12/8"};
static UI_UNUSED const int TS_NUM[] = {2, 3, 4, 5, 6, 7, 9, 12}, TS_DEN[] = {4, 4, 4, 4, 8, 8, 8, 8};
static UI_UNUSED const char *const KIND_ABOUT[] = {
    "the low notes under everything", "the main tune people hum", "long, soft background chords",
    "piano-like, for chords and melodies", "short notes for riffs and arpeggios", "voices: choirs, oohs and aahs, talking synths",
    "single drum hits to build beats",
    "whooshes, booms and effects for transitions"};

#endif
