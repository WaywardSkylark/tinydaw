/* tinydaw: undo and redo. */
#include "ui.h"

State undo_stack[UNDO_MAX], redo_stack[UNDO_MAX];
int nundo, nredo;
State committed;                 /* the state as of the last check */
State saved;                     /* the state when last saved or opened */
int saved_nsamples, edit_serial;

void state_free(State *s)
{
    free(s->clips);
    free(s->notes);
    free(s->autos);
    free(s->drums);
    free(s->chords);
    free(s->bends);
    s->bends = NULL;
    s->nbends = 0;
    free(s->slots);
    s->slots = NULL;
    s->nscenes = 0;
    s->clips = NULL;
    s->notes = NULL;
    s->autos = NULL;
    s->drums = NULL;
    s->chords = NULL;
    s->nclips = s->nnotes = s->nautos = s->ndrums = s->nchords = 0;
}

void *dup_mem(const void *src, size_t n)   /* malloc'd copy; never NULL for n = 0 */
{
    void *d = malloc(n ? n : 1);
    if (d && n) memcpy(d, src, n);
    return d;
}

void state_capture(State *s)
{
    free(s->clips);
    s->clips = malloc((size_t)(P.nclips > 0 ? P.nclips : 1) * sizeof(Clip));
    if (s->clips) memcpy(s->clips, P.clips, (size_t)P.nclips * sizeof(Clip));
    s->nclips = s->clips ? P.nclips : 0;
    free(s->notes);
    free(s->autos);
    s->notes = dup_mem(P.notes, (size_t)P.nnotes * sizeof(Note));
    s->nnotes = s->notes ? P.nnotes : 0;
    s->autos = dup_mem(P.autos, (size_t)P.nautos * sizeof(AutoPoint));
    s->nautos = s->autos ? P.nautos : 0;
    free(s->drums);
    s->drums = dup_mem(P.drums, (size_t)P.ndrums * sizeof(DrumPat));
    s->ndrums = s->drums ? P.ndrums : 0;
    memcpy(s->sections, P.sections, sizeof s->sections);
    s->nsections = P.nsections;
    free(s->chords);
    s->chords = dup_mem(P.chords, (size_t)P.nchords * sizeof(SongChord));
    s->nchords = s->chords ? P.nchords : 0;
    free(s->bends);
    s->bends = dup_mem(P.bends, (size_t)P.nbends * sizeof(Bend));
    s->nbends = s->bends ? P.nbends : 0;
    memcpy(s->tags, P.tags, sizeof s->tags);
    memcpy(s->buses, P.buses, sizeof s->buses);
    s->nbuses = P.nbuses;
    free(s->slots);
    s->slots = P.nscenes ? dup_mem(P.slots, sizeof P.slots) : NULL;
    s->nscenes = P.nscenes;
    memcpy(s->scene_names, P.scene_names, sizeof s->scene_names);
    s->key_root = P.key_root;
    s->key_scale = P.key_scale;
    s->nkeys = P.nkeys;
    memcpy(s->keys, P.keys, sizeof s->keys);
    memcpy(s->tracks, P.tracks, sizeof s->tracks);
    s->ntracks = P.ntracks;
    s->bpm = P.bpm;
    s->ts_num = P.ts_num;
    s->ts_den = P.ts_den;
    s->nchanges = P.nchanges;
    memcpy(s->changes, P.changes, sizeof s->changes);
    s->a4 = P.a4;
    s->loop_start = P.loop_start;
    s->loop_end = P.loop_end;
    memcpy(s->pads, P.pads, sizeof s->pads);
    s->inst_sample = P.inst_sample;
    s->inst_root = P.inst_root;
    s->inst_track = P.inst_track;
    s->fx = P.fx;
    for (int i = 0; i < MAX_SAMPLES; i++)   /* bit 1: removed, bit 2: listed */
        s->removed[i] = i < P.nsamples ? (P.samples[i].removed != 0) | (P.samples[i].listed != 0) << 1 : 0;
    s->label[0] = 0;
}

int removed_same(const State *s)
{
    for (int i = 0; i < P.nsamples; i++)
        if (s->removed[i] != ((P.samples[i].removed != 0) | (P.samples[i].listed != 0) << 1)) return 0;
    return 1;
}

int state_same(const State *s)
{
    return s->nclips == P.nclips && !memcmp(s->clips, P.clips, (size_t)P.nclips * sizeof(Clip)) &&
           s->nnotes == P.nnotes && !memcmp(s->notes, P.notes, (size_t)P.nnotes * sizeof(Note)) &&
           s->nautos == P.nautos && !memcmp(s->autos, P.autos, (size_t)P.nautos * sizeof(AutoPoint)) &&
           s->ndrums == P.ndrums && !memcmp(s->drums, P.drums, (size_t)P.ndrums * sizeof(DrumPat)) &&
           s->nsections == P.nsections && !memcmp(s->sections, P.sections, (size_t)P.nsections * sizeof(Section)) &&
           s->nchords == P.nchords && !memcmp(s->chords, P.chords, (size_t)P.nchords * sizeof(SongChord)) &&
           s->nbends == P.nbends && !memcmp(s->bends, P.bends, (size_t)P.nbends * sizeof(Bend)) &&
           !strcmp(s->tags, P.tags) && s->nbuses == P.nbuses &&
           !memcmp(s->buses, P.buses, sizeof s->buses) && s->nscenes == P.nscenes && (!P.nscenes || (s->slots && !memcmp(s->slots, P.slots, sizeof P.slots))) &&
           !memcmp(s->scene_names, P.scene_names, sizeof s->scene_names) &&
           s->key_root == P.key_root && s->key_scale == P.key_scale && s->nkeys == P.nkeys &&
           !memcmp(s->keys, P.keys, (size_t)P.nkeys * sizeof(KeyChange)) &&
           s->ntracks == P.ntracks && !memcmp(s->tracks, P.tracks, sizeof s->tracks) && s->bpm == P.bpm && s->ts_num == P.ts_num && s->ts_den == P.ts_den && s->nchanges == P.nchanges &&
           !memcmp(s->changes, P.changes, (size_t)P.nchanges * sizeof(MeterChange)) &&
           s->a4 == P.a4 && s->loop_start == P.loop_start && s->loop_end == P.loop_end &&
           !memcmp(s->pads, P.pads, sizeof s->pads) && s->inst_sample == P.inst_sample &&
           s->inst_root == P.inst_root && s->inst_track == P.inst_track && !memcmp(&s->fx, &P.fx, sizeof s->fx) &&
           removed_same(s);
}

void state_restore(const State *s)
{
    LOCK();
    memcpy(P.clips, s->clips, (size_t)s->nclips * sizeof(Clip));
    P.nclips = s->nclips;
    memcpy(P.notes, s->notes, (size_t)s->nnotes * sizeof(Note));
    P.nnotes = s->nnotes;
    memcpy(P.autos, s->autos, (size_t)s->nautos * sizeof(AutoPoint));
    P.nautos = s->nautos;
    memcpy(P.drums, s->drums, (size_t)s->ndrums * sizeof(DrumPat));
    P.ndrums = s->ndrums;
    memcpy(P.sections, s->sections, sizeof P.sections);
    P.nsections = s->nsections;
    memcpy(P.chords, s->chords, (size_t)s->nchords * sizeof(SongChord));
    P.nchords = s->nchords;
    memcpy(P.bends, s->bends, (size_t)s->nbends * sizeof(Bend));
    P.nbends = s->nbends;
    memcpy(P.tags, s->tags, sizeof P.tags);
    memcpy(P.buses, s->buses, sizeof P.buses);
    P.nbuses = s->nbuses;
    if (s->slots) memcpy(P.slots, s->slots, sizeof P.slots);
    else memset(P.slots, 0, sizeof P.slots);
    P.nscenes = s->nscenes;
    memcpy(P.scene_names, s->scene_names, sizeof P.scene_names);
    for (int t = 0; t < MAX_TRACKS; t++)        /* a slot that's gone isn't playing */
        if (P.sess_play[t] >= P.nscenes || (P.sess_play[t] >= 0 && P.slots[t][P.sess_play[t]].kind == SLOT_EMPTY))
            P.sess_play[t] = SESS_TIMELINE;
    P.key_root = s->key_root;
    P.key_scale = s->key_scale;
    P.nkeys = s->nkeys;
    memcpy(P.keys, s->keys, sizeof P.keys);
    memcpy(P.tracks, s->tracks, sizeof s->tracks);
    P.ntracks = s->ntracks;
    P.bpm = s->bpm;
    P.ts_num = s->ts_num;
    P.ts_den = s->ts_den;
    P.nchanges = s->nchanges;
    memcpy(P.changes, s->changes, sizeof P.changes);
    P.a4 = s->a4;
    P.loop_start = s->loop_start;
    P.loop_end = s->loop_end;
    memcpy(P.pads, s->pads, sizeof s->pads);
    P.inst_sample = s->inst_sample;
    P.inst_root = s->inst_root;
    P.inst_track = s->inst_track;
    P.fx = s->fx;
    for (int i = 0; i < P.nsamples; i++) {
        P.samples[i].removed = s->removed[i] & 1;
        P.samples[i].listed = s->removed[i] >> 1 & 1;
    }
    UNLOCK();
}

/* What changed between a snapshot and the project now, in a few words. */
void describe(const State *s, char *buf, size_t n)
{
    if (!removed_same(s)) {
        int back = 0;                       /* a sound came (back) into the library, or went */
        for (int i = 0; i < P.nsamples; i++)
            back |= ((s->removed[i] & 1) && !P.samples[i].removed) || (!(s->removed[i] & 2) && P.samples[i].listed);
        snprintf(buf, n, back ? "paste a sound" : "remove a sound");
    }
    else if (s->nnotes != P.nnotes || memcmp(s->notes, P.notes, (size_t)P.nnotes * sizeof(Note)))
        snprintf(buf, n, "%s", P.nnotes > s->nnotes ? "add notes" : P.nnotes < s->nnotes ? "delete notes" : "note edit");
    else if (s->nautos != P.nautos || memcmp(s->autos, P.autos, (size_t)P.nautos * sizeof(AutoPoint)))
        snprintf(buf, n, "automation");
    else if (s->nsections != P.nsections || memcmp(s->sections, P.sections, (size_t)P.nsections * sizeof(Section)))
        snprintf(buf, n, "%s", P.nsections > s->nsections ? "add section" : P.nsections < s->nsections ? "remove section" : "sections");
    else if (s->nbends != P.nbends || memcmp(s->bends, P.bends, (size_t)P.nbends * sizeof(Bend)))
        snprintf(buf, n, "pitch bend");
    else if (s->nscenes != P.nscenes || (P.nscenes && (!s->slots || memcmp(s->slots, P.slots, sizeof P.slots))) ||
             memcmp(s->scene_names, P.scene_names, sizeof s->scene_names))
        snprintf(buf, n, "session");
    else if (strcmp(s->tags, P.tags))
        snprintf(buf, n, "tags");
    else if (s->nbuses != P.nbuses || memcmp(s->buses, P.buses, sizeof s->buses))
        snprintf(buf, n, "buses");
    else if (s->nchords != P.nchords || memcmp(s->chords, P.chords, (size_t)P.nchords * sizeof(SongChord)))
        snprintf(buf, n, "%s", P.nchords > s->nchords ? "add chord" : P.nchords < s->nchords ? "remove chord" : "chords");
    else if (s->key_root != P.key_root || s->key_scale != P.key_scale || s->nkeys != P.nkeys ||
             memcmp(s->keys, P.keys, (size_t)P.nkeys * sizeof(KeyChange)))
        snprintf(buf, n, "key");
    else if (s->ndrums != P.ndrums || memcmp(s->drums, P.drums, (size_t)P.ndrums * sizeof(DrumPat)))
        snprintf(buf, n, "%s", P.ndrums > s->ndrums ? "new drum pattern" : P.ndrums < s->ndrums ? "delete drum pattern" : "drum steps");
    else if (P.nclips > s->nclips) snprintf(buf, n, "add %d clip%s", P.nclips - s->nclips, P.nclips - s->nclips == 1 ? "" : "s");
    else if (P.nclips < s->nclips) snprintf(buf, n, "delete %d clip%s", s->nclips - P.nclips, s->nclips - P.nclips == 1 ? "" : "s");
    else if (memcmp(s->clips, P.clips, (size_t)P.nclips * sizeof(Clip))) snprintf(buf, n, "clip edit");
    else if (s->ntracks != P.ntracks) snprintf(buf, n, "add track");
    else if (memcmp(s->tracks, P.tracks, sizeof s->tracks)) snprintf(buf, n, "track settings");
    else if (memcmp(&s->fx, &P.fx, sizeof s->fx)) snprintf(buf, n, "effects");
    else if (s->bpm != P.bpm) snprintf(buf, n, "tempo %.0f BPM", s->bpm);
    else if (s->ts_num != P.ts_num || s->ts_den != P.ts_den || s->nchanges != P.nchanges ||
             memcmp(s->changes, P.changes, (size_t)P.nchanges * sizeof(MeterChange)))
        snprintf(buf, n, "tempo / time signature change");
    else if (s->loop_start != P.loop_start || s->loop_end != P.loop_end) snprintf(buf, n, "loop region");
    else if (memcmp(s->pads, P.pads, sizeof s->pads) || s->inst_sample != P.inst_sample || s->inst_root != P.inst_root)
        snprintf(buf, n, "pad settings");
    else snprintf(buf, n, "tuning");
}

void stack_push(State *stack, int *n, State s)
{
    if (*n == UNDO_MAX) {                   /* forget the oldest step */
        state_free(&stack[0]);
        memmove(stack, stack + 1, (UNDO_MAX - 1) * sizeof *stack);
        (*n)--;
    }
    stack[(*n)++] = s;
}

void history_check(void)
{
    if (state_same(&committed)) return;
    State before = committed;
    describe(&before, before.label, sizeof before.label);
    stack_push(undo_stack, &nundo, before);
    while (nredo > 0) state_free(&redo_stack[--nredo]);
    memset(&committed, 0, sizeof committed);
    state_capture(&committed);
    edit_serial++;
}

/* The pitch curves' playback tables, rebuilt when a curve changed: the work without the lock, the swap with it. */
void bends_update(void)
{
    BendWork *w = bends_build(&P);
    if (!w) return;
    LOCK();
    bends_install(w);
    UNLOCK();
}

int th_synced_root = -1, th_synced_scale = -1;   /* the key Theory showed last frame (-1: take the song's) */

void history_reset(void)
{
    sess_forget();
    th_synced_root = th_synced_scale = -1;
    while (nundo > 0) state_free(&undo_stack[--nundo]);
    while (nredo > 0) state_free(&redo_stack[--nredo]);
    state_capture(&committed);
}

void undo_redo(int redo)
{
    tempo_from = 0;                         /* a tempo edit in progress is overtaken */
    State *from = redo ? redo_stack : undo_stack, *to = redo ? undo_stack : redo_stack;
    int *nfrom = redo ? &nredo : &nundo, *nto = redo ? &nundo : &nredo;
    if (drag.kind != D_NONE) return;
    history_check();                        /* an edit made this frame becomes its own step first */
    if (*nfrom == 0) {
        status(redo ? "Nothing to redo" : "Nothing to undo");
        return;
    }
    State target = from[--*nfrom];
    State current = committed;              /* committed == the project now */
    snprintf(current.label, sizeof current.label, "%s", target.label);
    stack_push(to, nto, current);
    state_restore(&target);
    committed = target;
    committed.label[0] = 0;
    sel_clear();
    memset(nsel, 0, sizeof nsel);
    edit_serial++;
    status("%s: %s   (%d more to undo, %d to redo)", redo ? "Redo" : "Undo", target.label, nundo, nredo);
}

/* After a project rate change every snapshot's positions are rescaled like the project's. */
void state_scale(State *s, double ratio)
{
    for (int c = 0; c < s->nclips; c++) {
        Clip *cl = &s->clips[c];
        long frames = P.samples[cl->sample].frames;
        cl->start = llround(cl->start * ratio);
        cl->offset = llround(cl->offset * ratio);
        cl->length = llround(cl->length * ratio);
        if (cl->pattern) continue;
        if (cl->offset > frames - 1) cl->offset = frames - 1;
        if (cl->offset + cl->length > frames) cl->length = frames - cl->offset;
        if (cl->length < 1) cl->length = 1;
    }
    for (int i = 0; i < s->nnotes; i++) {
        s->notes[i].start = llround(s->notes[i].start * ratio);
        s->notes[i].length = llround(s->notes[i].length * ratio);
        if (s->notes[i].length < 1) s->notes[i].length = 1;
    }
    for (int i = 0; i < s->nautos; i++) s->autos[i].at = llround(s->autos[i].at * ratio);
    s->loop_start = llround(s->loop_start * ratio);
    s->loop_end = llround(s->loop_end * ratio);
}

void history_scale(double ratio)
{
    for (int i = 0; i < nundo; i++) state_scale(&undo_stack[i], ratio);
    for (int i = 0; i < nredo; i++) state_scale(&redo_stack[i], ratio);
    state_scale(&committed, ratio);
    state_scale(&saved, ratio);
}
