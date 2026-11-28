/* tinydaw project files (.tdaw): a small line-based text format.
 *
 * Sounds are stored as recipes, not audio: generated tones, chords and drum loops are
 * synthesized again on load, imported sounds are read again from their files, and pitched
 * copies are made again from their source. That keeps project files tiny and lets a project
 * open at full quality at any rate. An imported file that has gone missing becomes silence of
 * the same length (and keeps its path, so it comes back once the file is restored). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "daw.h"
#include "compat.h"
#include "theory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAGIC "tinydaw-project 1"

void project_free(Project *p)
{
    for (int i = 0; i < p->nsamples; i++) {
        free(p->samples[i].data);
        free(p->samples[i].peak_min);
        free(p->samples[i].peak_max);
        p->samples[i].data = p->samples[i].peak_min = p->samples[i].peak_max = NULL;
    }
    p->nsamples = 0;
}

static const char *kind_of(const Sample *s)
{
    if (s->synth) return "synth";
    if (s->vocal) return "vocal";
    if (s->is_beat == 2) return "hit";
    if (s->is_beat) return "beat";
    if (s->generated && s->nchord) return "chord";
    if (s->generated) return "gen";
    if (s->pitched) return "variant";
    if (s->is_layer) return "layer";
    if (s->path[0]) return "file";
    return "raw";
}

int project_save(const Project *p, const char *path, char *msg, size_t msglen)
{
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) {
        snprintf(msg, msglen, "can't write %s: %s", path, strerror(errno));
        return -1;
    }
    fprintf(f, MAGIC "\n");
    fprintf(f, "saved-by tinydaw %s\n", TINYDAW_VERSION);   /* for people reading the file; ignored on load */
    fprintf(f, "rate %d\nbpm %.17g\na4 %.17g\nmaster %.9g\n", SR, p->bpm, p->a4, p->master);
    if (p->ts_num != 4 || p->ts_den != 4) fprintf(f, "meter %d %d\n", p->ts_num, p->ts_den);
    for (int i = 0; i < p->nchanges; i++)
        fprintf(f, "change %d %.17g %d %d\n", p->changes[i].bar, p->changes[i].bpm, p->changes[i].num, p->changes[i].den);
    fprintf(f, "loop %d %ld %ld\nplayhead %ld\n", p->loop, p->loop_start, p->loop_end, p->playhead);
    fprintf(f, "instrument %d %d %d\n", p->inst_sample, p->inst_root, p->inst_track);
    const MasterFx *fx = &p->fx;
    fprintf(f, "fx %d %.9g %.9g %.9g %d %.9g %.9g %.9g %d %.9g %.9g\n", fx->delay_div, fx->delay_feedback,
            fx->delay_tone, fx->delay_level, fx->pingpong, fx->reverb_size, fx->reverb_damp, fx->reverb_level,
            fx->sc_source, fx->sc_release, fx->bend_range);
    fprintf(f, "dynamics %d %.9g %.9g %.9g %.9g %.9g %d %.9g\n", fx->comp_on, fx->comp_thresh, fx->comp_ratio,
            fx->comp_attack, fx->comp_release, fx->comp_makeup, fx->limit_on, fx->limit_ceiling);
    for (int t = 0; t < p->ntracks; t++) {
        const Track *tr = &p->tracks[t];
        fprintf(f, "track %.9g %d %d %s\n", tr->volume, tr->mute, tr->solo, tr->name);
        fprintf(f, "trackfx %.9g %d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", tr->pan, tr->filter, tr->cutoff,
                tr->reso, tr->eq[0], tr->eq[1], tr->eq[2], tr->drive, tr->delay_send, tr->reverb_send, tr->duck);
        for (int m = 0; m < TRACK_MACROS; m++) {    /* after the track line they belong to */
            const Macro *mc = &tr->macro[m];
            if (!mc->name[0] && !mc->nlinks) continue;
            fprintf(f, "macro %d %.6g %s\n", m, mc->value, mc->name);
            for (int k = 0; k < mc->nlinks; k++)
                fprintf(f, "mlink %d %d %.9g %.9g\n", m, mc->link[k].param, mc->link[k].lo, mc->link[k].hi);
        }
        fprintf(f, "trackdyn %.9g %d %.9g %.9g %.9g %.9g %.9g\n", tr->width, tr->comp_on, tr->comp_thresh,
                tr->comp_ratio, tr->comp_attack, tr->comp_release, tr->comp_makeup);
    }
    int raw = 0;
    for (int i = 0; i < p->nsamples; i++) {
        const Sample *s = &p->samples[i];
        const char *kind = kind_of(s);
        raw += !strcmp(kind, "raw");
        fprintf(f, "sample %s %ld %s\n", kind, s->frames, s->name);
        if (s->generated) {
            const GenParams *g = &s->gen;
            fprintf(f, "gen %d %d %.17g %.17g %.17g %.17g %.17g %.17g\n", g->wave, g->note, g->beats, g->volume,
                    g->attack_ms, g->release_ms, g->a4, s->gen_bpm);
        }
        if (s->nchord) {
            fprintf(f, "chord %d", s->nchord);
            for (int k = 0; k < s->nchord; k++) fprintf(f, " %d", s->chord[k]);
            fprintf(f, "\n");
        }
        if (s->is_beat) {
            const BeatSpec *b = &s->beat;
            fprintf(f, "beat %d %.17g %d %.17g %.17g %.17g %.17g %.17g %.17g %d\n", b->steps, b->swing, b->swing8,
                    b->kick_hz, b->kick_decay, b->snare_hz, b->hat_decay, b->open_decay, b->volume, s->beat_mask);
            for (int l = 0; l < DRUM_LANES; l++) fprintf(f, "lane %d %s\n", l, b->lanes[l][0] ? b->lanes[l] : "-");
        }
        if (s->synth) {
            const SynthSound *y = &s->syn;
            char pt[1500];
            synth_patch_text(&y->patch, pt, sizeof pt);
            fprintf(f, "synth %d %.9g %.9g %.9g", y->note, y->seconds, y->volume, y->a4);
            for (int m = 0; m < MACROS; m++) fprintf(f, " %.6g", y->macro[m]);
            fprintf(f, "\npatch%s\n", pt);
        }
        if (s->vocal) {
            const VocalSpec *v = &s->voc;
            fprintf(f, "vocal %d %d %d %.6g %.6g %.9g %.17g %d %d %.6g %d\nwords %s\n", v->style, v->high, v->lang,
                    v->vibrato, v->volume, v->a4, v->bpm, v->tune, v->first, v->step, v->singer, v->words);
            for (int k = 0; k < v->nnotes; k++)
                fprintf(f, "vnote %d %.9g %.9g\n", v->notes[k].note, v->notes[k].start, v->notes[k].len);
            for (int k = 0; k < v->nbend; k++) fprintf(f, "vbend %.9g %.6g\n", v->bend_beat[k], v->bend_semis[k]);
        }
        if (s->pitched) fprintf(f, "variant %d %d %d %.17g\n", s->src, s->semis, s->reversed, s->stretch > 0 ? s->stretch : 1);
        if (s->is_layer) fprintf(f, "layer %d %d %d %d\n", s->src, s->layer_lvl, s->layer_n, s->layer_rr);
        if (s->nvel > 0 && s->nrr > 0) {
            fprintf(f, "layers %d %d", s->nvel, s->nrr);
            for (int l = 0; l < MAX_VEL; l++)
                for (int r = 0; r < MAX_RR; r++) fprintf(f, " %d", s->layer[l][r]);
            fprintf(f, "\n");
        }
        if (s->hidden) fprintf(f, "hidden\n");
        if (s->path[0]) fprintf(f, "path %s\n", s->path);
        if (s->removed) fprintf(f, "removed\n");
        if (s->listed) fprintf(f, "listed\n");
        fprintf(f, "end\n");
    }
    for (int i = 0; i < p->ndrums; i++) {        /* drum patterns, before the clips that play them */
        const DrumPat *d = &p->drums[i];
        fprintf(f, "drum %d %d %d %d %.6g %s\n", d->id, d->track, d->steps, d->nrows, d->swing, d->name);
        for (int r = 0; r < d->nrows; r++) {
            fprintf(f, "drow %d %d %.6g", r, d->sample[r], d->row_vol[r]);
            for (int k = 0; k < d->steps; k++) fprintf(f, " %d:%d", d->vel[r][k], d->prob[r][k]);
            fprintf(f, "\n");
        }
    }
    for (int i = 0; i < p->nsections; i++)
        fprintf(f, "section %d %d %s\n", p->sections[i].bar, p->sections[i].bars, p->sections[i].name);
    if (p->tags[0]) fprintf(f, "tags %s\n", p->tags);
    for (int i = 0; i < p->nscenes; i++) fprintf(f, "scene %d %s\n", i, p->scene_names[i]);
    for (int t = 0; t < p->ntracks; t++)
        for (int i = 0; i < p->nscenes; i++) {
            const Slot *sl = &p->slots[t][i];
            if (sl->kind == SLOT_EMPTY) continue;
            const Clip *c = &sl->clip;
            fprintf(f, "slot %d %d %d %.9g %d %d %d %d %ld %ld %.9g %d %s\n", t, i, sl->kind, sl->beats, sl->bar, c->sample,
                    c->pattern, c->root, c->offset, c->length, c->gain_db, c->bend, sl->name);
        }
    if (p->key_root || p->key_scale) fprintf(f, "key %d %d\n", p->key_root, p->key_scale);
    for (int i = 0; i < p->nkeys; i++) fprintf(f, "keychange %d %d %d\n", p->keys[i].bar, p->keys[i].root, p->keys[i].scale);
    for (int i = 0; i < p->nchords; i++) {
        const SongChord *c = &p->chords[i];
        fprintf(f, "harmony %.9g %.9g %d %d %d\n", c->at, c->len, c->root, c->type, c->bass);
    }
    for (int i = 0; i < p->nbends; i++) {        /* pitch curves the clips use, before the clips */
        const Bend *b = &p->bends[i];
        int used = 0;
        used = bend_used(p, b->id);
        if (!used) continue;
        fprintf(f, "bend %d %d\n", b->id, b->keep_time);
        for (int k = 0; k < b->n; k++) fprintf(f, "bpt %.9g %.6g\n", b->at[k], b->semis[k]);
    }
    for (int c = 0; c < p->nclips; c++) {
        const Clip *cl = &p->clips[c];
        fprintf(f, "clip %d %d %ld %ld %ld %.9g %ld %ld", cl->sample, cl->track, cl->start, cl->offset, cl->length,
                cl->gain_db, cl->fade_in, cl->fade_out);
        if (cl->pattern || cl->bend) fprintf(f, " %d %d", cl->pattern, cl->root);
        if (cl->bend) fprintf(f, " %d", cl->bend);
        fprintf(f, "\n");
    }
    for (int i = 0; i < p->nnotes; i++) {
        const Note *n = &p->notes[i];
        fprintf(f, "note %d %d %ld %ld %.6g\n", n->pattern, n->note, n->start, n->length, n->vel);
    }
    for (int i = 0; i < p->nautos; i++) {
        const AutoPoint *a = &p->autos[i];
        fprintf(f, "auto %d %d %ld %.6g\n", a->track, a->param, a->at, a->v);
    }
    for (int i = 0; i < NPADS; i++) {
        const Pad *pd = &p->pads[i];
        fprintf(f, "pad %d %d %d %.9g %d %d %.9g\n", i, pd->sample, pd->mode, pd->volume, pd->pitch, pd->track, pd->pan);
    }
    int bad = ferror(f);
    if (fclose(f) != 0 || bad || replace_file(tmp, path) != 0) {   /* write then rename: never a half-written file */
        snprintf(msg, msglen, "can't write %s: %s", path, strerror(errno));
        remove(tmp);
        return -1;
    }
    const char *base = path_base(path);
    snprintf(msg, msglen, "Saved %s (%d clips, %d sounds)%s", base, p->nclips, p->nsamples,
             raw ? " - some sounds have no recipe and will load as silence" : "");
    return 0;
}

/* ---------------------------------------------------------------- loading */

typedef struct {
    char kind[16], name[96], path[1024];
    long frames;
    int has_gen, has_beat, has_synth, has_vocal;
    SynthSound syn;
    VocalSpec voc;
    GenParams gen;
    double gen_bpm;
    int chord[6], nchord;
    BeatSpec beat;
    int mask, src, semis, reversed, removed, listed, hidden;
    double stretch;
    int lvl, ln, lrr;                     /* a layer */
    int nvel, nrr, table[MAX_VEL][MAX_RR];   /* its layers */
} Pending;

static const char *rest_after(const char *line, int fields)   /* the text after n words */
{
    const char *s = line;
    for (int i = 0; i < fields; i++) {
        while (*s == ' ') s++;
        while (*s && *s != ' ') s++;
    }
    return *s == ' ' ? s + 1 : s;
}

static int silence(Project *p, const char *name, long frames)
{
    if (frames < 16) frames = 16;
    if (frames > 60L * 60 * 384000) frames = 60L * 60 * 384000;
    float *d = calloc((size_t)frames * 2, sizeof(float));
    return d ? sample_add(p, name, d, frames) : -1;
}

static int unsung;                       /* sung lines that couldn't be sung (no eSpeak NG) */

/* Makes the pending sound; returns its index or -1. *missing counts files not found. */
static int build(Project *p, Pending *s, int *missing)
{
    int idx = -1, before = p->nsamples;
    double keep_bpm = p->bpm;
    if (!strcmp(s->kind, "gen") && s->has_gen) {
        p->bpm = s->gen_bpm;
        idx = sample_generate(p, &s->gen);
    } else if (!strcmp(s->kind, "chord") && s->has_gen && s->nchord > 0) {
        p->bpm = s->gen_bpm;
        idx = sample_generate_notes(p, &s->gen, s->chord, s->nchord, s->name);
    } else if (!strcmp(s->kind, "beat") && s->has_beat) {
        p->bpm = s->gen_bpm;
        idx = sample_generate_beat(p, &s->beat, s->mask, s->name);
    } else if (!strcmp(s->kind, "vocal") && s->has_vocal) {
        char err[200];
        err[0] = 0;
        idx = sample_generate_vocal(p, &s->voc, s->name, err, sizeof err);
        if (idx >= 0 && err[0]) unsung++;      /* kept as silence */
    } else if (!strcmp(s->kind, "synth") && s->has_synth) {
        idx = sample_generate_synth(p, &s->syn, s->name);
    } else if (!strcmp(s->kind, "hit") && s->has_beat) {
        idx = sample_generate_hit(p, &s->beat, s->mask, s->name);
    } else if ((!strcmp(s->kind, "pitched") || !strcmp(s->kind, "variant")) && s->src >= 0 && s->src < p->nsamples) {
        idx = sample_variant_ex(p, s->src, s->semis, s->reversed, s->stretch > 0 ? s->stretch : 1);
    } else if (!strcmp(s->kind, "layer") && s->src >= 0 && s->src < p->nsamples) {
        idx = sample_layer(p, s->src, s->lvl, s->ln, s->lrr);
    } else if (!strcmp(s->kind, "file") && s->path[0]) {
        char err[200];
        idx = sample_import(p, s->path, err, sizeof err);
        if (idx < 0) {
            ++*missing;
            idx = silence(p, s->name, s->frames);
            if (idx >= 0) snprintf(p->samples[idx].path, sizeof p->samples[idx].path, "%s", s->path);
        }
    }
    p->bpm = keep_bpm;
    if (idx >= 0 && idx < before) idx = -1;      /* must be a new sound, or later indices shift */
    if (idx < 0) idx = silence(p, s->name, s->frames);
    if (idx >= 0) snprintf(p->samples[idx].name, sizeof p->samples[idx].name, "%s", s->name);
    if (idx >= 0) p->samples[idx].removed = s->removed;
    if (idx >= 0) p->samples[idx].listed = s->listed;
    if (idx >= 0) p->samples[idx].hidden = s->hidden;
    if (idx >= 0 && s->nvel > 0 && s->nrr > 0) {   /* its layers come later: checked once all are in */
        memcpy(p->samples[idx].layer, s->table, sizeof s->table);
        p->samples[idx].nvel = s->nvel;
        p->samples[idx].nrr = s->nrr;
    }
    return idx;
}

int project_load(Project *p, const char *path, char *msg, size_t msglen)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(msg, msglen, "can't open %s: %s", path, strerror(errno));
        return -1;
    }
    char line[2048];
    if (!fgets(line, sizeof line, f) || strncmp(line, MAGIC, strlen(MAGIC)) != 0) {
        fclose(f);
        snprintf(msg, msglen, "%s is not a tinydaw project", path);
        return -1;
    }
    int old_rate = g_rate, ntracks = 0, missing = 0, in_sample = 0, lineno = 1;
    unsung = 0;
    Pending s;
    project_init(p);
    p->ntracks = 0;

    while (fgets(line, sizeof line, f)) {
        lineno++;
        line[strcspn(line, "\r\n")] = 0;
        char word[16] = "";
        sscanf(line, "%15s", word);
        int ok = 1;
        if (!word[0]) continue;
        if (!strcmp(word, "rate")) {
            int r = 0;
            ok = sscanf(line, "rate %d", &r) == 1 && r >= 8000 && r <= 384000;
            if (ok) g_rate = r;
        } else if (!strcmp(word, "bpm")) {
            ok = sscanf(line, "bpm %lf", &p->bpm) == 1 && p->bpm >= 20 && p->bpm <= 999;
        } else if (!strcmp(word, "meter")) {
            ok = sscanf(line, "meter %d %d", &p->ts_num, &p->ts_den) == 2 && p->ts_num >= 1 && p->ts_num <= 32 &&
                 (p->ts_den == 2 || p->ts_den == 4 || p->ts_den == 8 || p->ts_den == 16);
            if (!ok) p->ts_num = p->ts_den = 4;
        } else if (!strcmp(word, "change")) {
            MeterChange c;
            ok = sscanf(line, "change %d %lf %d %d", &c.bar, &c.bpm, &c.num, &c.den) == 4 && c.bar >= 1 &&
                 c.bpm >= 20 && c.bpm <= 999 && c.num >= 1 && c.num <= 32 && c.den >= 1 && p->nchanges < MAX_METER &&
                 (p->nchanges == 0 || c.bar > p->changes[p->nchanges - 1].bar);
            if (ok) p->changes[p->nchanges++] = c;
        } else if (!strcmp(word, "a4")) {
            ok = sscanf(line, "a4 %lf", &p->a4) == 1;
        } else if (!strcmp(word, "master")) {
            ok = sscanf(line, "master %f", &p->master) == 1;
        } else if (!strcmp(word, "loop")) {
            ok = sscanf(line, "loop %d %ld %ld", &p->loop, &p->loop_start, &p->loop_end) == 3;
        } else if (!strcmp(word, "playhead")) {
            ok = sscanf(line, "playhead %ld", &p->playhead) == 1;
        } else if (!strcmp(word, "instrument")) {
            ok = sscanf(line, "instrument %d %d %d", &p->inst_sample, &p->inst_root, &p->inst_track) >= 2;
        } else if (!strcmp(word, "fx")) {
            MasterFx *fx = &p->fx;
            ok = sscanf(line, "fx %d %f %f %f %d %f %f %f %d %f %f", &fx->delay_div, &fx->delay_feedback,
                        &fx->delay_tone, &fx->delay_level, &fx->pingpong, &fx->reverb_size, &fx->reverb_damp,
                        &fx->reverb_level, &fx->sc_source, &fx->sc_release, &fx->bend_range) == 11;
            if (fx->delay_div < 0 || fx->delay_div >= DELAY_DIVS) fx->delay_div = 4;
            if (fx->sc_source >= MAX_TRACKS) fx->sc_source = -1;
        } else if (!strcmp(word, "dynamics")) {
            MasterFx *fx = &p->fx;
            ok = sscanf(line, "dynamics %d %f %f %f %f %f %d %f", &fx->comp_on, &fx->comp_thresh, &fx->comp_ratio,
                        &fx->comp_attack, &fx->comp_release, &fx->comp_makeup, &fx->limit_on, &fx->limit_ceiling) == 8;
        } else if (!strcmp(word, "trackdyn")) {
            if (ntracks > 0) {
                Track *t = &p->tracks[ntracks - 1];
                ok = sscanf(line, "trackdyn %f %d %f %f %f %f %f", &t->width, &t->comp_on, &t->comp_thresh, &t->comp_ratio,
                            &t->comp_attack, &t->comp_release, &t->comp_makeup) == 7;
            }
        } else if (!strcmp(word, "trackfx")) {
            if (ntracks > 0) {
                Track *t = &p->tracks[ntracks - 1];
                ok = sscanf(line, "trackfx %f %d %f %f %f %f %f %f %f %f %f", &t->pan, &t->filter, &t->cutoff, &t->reso,
                            &t->eq[0], &t->eq[1], &t->eq[2], &t->drive, &t->delay_send, &t->reverb_send, &t->duck) == 11;
                if (t->filter < 0 || t->filter >= FILT_TYPES) t->filter = FILT_OFF;
            }
        } else if (!strcmp(word, "macro")) {
            int m = -1, nm = 0;
            float v = 0;
            ok = ntracks > 0 && sscanf(line, "macro %d %f %n", &m, &v, &nm) >= 2 && m >= 0 && m < TRACK_MACROS;
            if (ok) {
                Macro *mc = &p->tracks[ntracks - 1].macro[m];
                mc->value = v < 0 ? 0 : v > 1 ? 1 : v;
                if (nm > 0) snprintf(mc->name, sizeof mc->name, "%.15s", line + nm);
                mc->name[strcspn(mc->name, "\r\n")] = 0;
            }
        } else if (!strcmp(word, "mlink")) {
            int m = -1;
            MacroLink l;
            ok = ntracks > 0 && sscanf(line, "mlink %d %d %f %f", &m, &l.param, &l.lo, &l.hi) == 4 && m >= 0 &&
                 m < TRACK_MACROS && l.param >= 0 && l.param < TP_COUNT;
            Macro *mc = ok ? &p->tracks[ntracks - 1].macro[m] : NULL;
            if (mc && mc->nlinks < MACRO_LINKS) mc->link[mc->nlinks++] = l;
        } else if (!strcmp(word, "track")) {
            if (ntracks < MAX_TRACKS) {
                Track *t = &p->tracks[ntracks];
                ok = sscanf(line, "track %f %d %d", &t->volume, &t->mute, &t->solo) == 3;
                snprintf(t->name, sizeof t->name, "%.31s", rest_after(line, 4));
                ntracks++;
            }
        } else if (!strcmp(word, "sample")) {
            memset(&s, 0, sizeof s);
            s.src = -1;
            ok = sscanf(line, "sample %15s %ld", s.kind, &s.frames) == 2;
            snprintf(s.name, sizeof s.name, "%.95s", rest_after(line, 3));
            in_sample = ok;
        } else if (in_sample && !strcmp(word, "gen")) {
            GenParams *g = &s.gen;
            ok = sscanf(line, "gen %d %d %lf %lf %lf %lf %lf %lf", &g->wave, &g->note, &g->beats, &g->volume,
                        &g->attack_ms, &g->release_ms, &g->a4, &s.gen_bpm) == 8 && g->wave >= 0 &&
                 g->wave < WAVE_COUNT && s.gen_bpm > 0;
            s.has_gen = ok;
        } else if (in_sample && !strcmp(word, "chord")) {
            int *c = s.chord;
            int n = sscanf(line, "chord %d %d %d %d %d %d %d", &s.nchord, &c[0], &c[1], &c[2], &c[3], &c[4], &c[5]);
            ok = s.nchord >= 1 && s.nchord <= 6 && n == s.nchord + 1;
        } else if (in_sample && !strcmp(word, "beat")) {
            BeatSpec *b = &s.beat;
            ok = sscanf(line, "beat %d %lf %d %lf %lf %lf %lf %lf %lf %d", &b->steps, &b->swing, &b->swing8,
                        &b->kick_hz, &b->kick_decay, &b->snare_hz, &b->hat_decay, &b->open_decay, &b->volume,
                        &s.mask) == 10;
            s.has_beat = ok;
        } else if (in_sample && !strcmp(word, "synth")) {
            SynthSound *y = &s.syn;
            double *m = y->macro;
            ok = sscanf(line, "synth %d %lf %lf %lf %lf %lf %lf %lf %lf", &y->note, &y->seconds, &y->volume, &y->a4,
                        &m[0], &m[1], &m[2], &m[3], &m[4]) == 4 + MACROS &&
                 y->note >= 0 && y->note <= 127 && y->seconds > 0;
            s.has_synth = ok;
        } else if (in_sample && !strcmp(word, "vocal")) {
            VocalSpec *v = &s.voc;
            ok = sscanf(line, "vocal %d %d %d %lf %lf %lf %lf %d %d %lf %d", &v->style, &v->high, &v->lang, &v->vibrato,
                        &v->volume, &v->a4, &v->bpm, &v->tune, &v->first, &v->step, &v->singer) >= 10 &&
                 v->style >= 0 && v->style < VS_STYLES && v->bpm > 0;
            s.has_vocal = ok;
        } else if (in_sample && !strcmp(word, "words")) {
            snprintf(s.voc.words, sizeof s.voc.words, "%.255s", rest_after(line, 1));
        } else if (in_sample && !strcmp(word, "vnote")) {
            VocalSpec *v = &s.voc;
            if (v->nnotes < VOCAL_NOTES) {
                VocalNote *nt = &v->notes[v->nnotes];
                ok = sscanf(line, "vnote %d %lf %lf", &nt->note, &nt->start, &nt->len) == 3 && nt->len > 0;
                if (ok) v->nnotes++;
            }
        } else if (in_sample && !strcmp(word, "vbend")) {
            VocalSpec *v = &s.voc;
            float b, st;
            ok = sscanf(line, "vbend %f %f", &b, &st) == 2;
            if (ok && v->nbend < VOCAL_BENDS && (!v->nbend || b >= v->bend_beat[v->nbend - 1])) {
                v->bend_beat[v->nbend] = b;
                v->bend_semis[v->nbend++] = st < -24 ? -24 : st > 24 ? 24 : st;
            }
        } else if (in_sample && !strcmp(word, "patch")) {
            synth_patch_parse(rest_after(line, 1), &s.syn.patch);
        } else if (in_sample && !strcmp(word, "lane")) {
            int l = -1;
            char pat[33] = "";
            ok = sscanf(line, "lane %d %32s", &l, pat) == 2 && l >= 0 && l < DRUM_LANES;
            if (ok) snprintf(s.beat.lanes[l], 33, "%s", strcmp(pat, "-") ? pat : "");
        } else if (in_sample && !strcmp(word, "pitch")) {         /* older files */
            ok = sscanf(line, "pitch %d %d", &s.src, &s.semis) == 2;
        } else if (in_sample && !strcmp(word, "variant")) {
            s.stretch = 1;
            ok = sscanf(line, "variant %d %d %d %lf", &s.src, &s.semis, &s.reversed, &s.stretch) >= 3 && s.stretch > 0;
        } else if (in_sample && !strcmp(word, "layer")) {
            ok = sscanf(line, "layer %d %d %d %d", &s.src, &s.lvl, &s.ln, &s.lrr) == 4 && s.ln >= 1 && s.ln <= MAX_VEL &&
                 s.lvl >= 0 && s.lvl < s.ln && s.lrr >= 0 && s.lrr < MAX_RR;
        } else if (in_sample && !strcmp(word, "layers")) {
            int at = 0, used = 0;
            ok = sscanf(line, "layers %d %d %n", &s.nvel, &s.nrr, &at) >= 2 && s.nvel >= 1 && s.nvel <= MAX_VEL &&
                 s.nrr >= 1 && s.nrr <= MAX_RR;
            const char *q = line + at;
            for (int l = 0; ok && l < MAX_VEL; l++)
                for (int r = 0; ok && r < MAX_RR; r++) {
                    ok = sscanf(q, " %d%n", &s.table[l][r], &used) == 1;
                    q += used;
                }
        } else if (in_sample && !strcmp(word, "removed")) {
            s.removed = 1;
        } else if (in_sample && !strcmp(word, "listed")) {
            s.listed = 1;
        } else if (in_sample && !strcmp(word, "hidden")) {
            s.hidden = 1;
        } else if (in_sample && !strcmp(word, "path")) {
            snprintf(s.path, sizeof s.path, "%.1023s", rest_after(line, 1));
        } else if (in_sample && !strcmp(word, "end")) {
            if (!s.gen_bpm) s.gen_bpm = p->bpm;
            if (build(p, &s, &missing) < 0) {
                snprintf(msg, msglen, "out of memory loading sound \"%s\"", s.name);
                goto fail;
            }
            in_sample = 0;
        } else if (!strcmp(word, "clip")) {
            Clip c;
            memset(&c, 0, sizeof c);
            ok = sscanf(line, "clip %d %d %ld %ld %ld %f %ld %ld %d %d %d", &c.sample, &c.track, &c.start, &c.offset,
                        &c.length, &c.gain_db, &c.fade_in, &c.fade_out, &c.pattern, &c.root, &c.bend) >= 5 &&
                 c.sample >= 0 && c.sample < p->nsamples && c.track >= 0 && c.track < MAX_TRACKS;
            if (c.pattern < 0 && !drum_find(p, -c.pattern)) ok = 0;   /* a drum clip whose pattern is missing */
            if (ok && c.pattern && p->nclips < MAX_CLIPS) {   /* a note or drum clip */
                if (c.offset < 0) c.offset = 0;
                if (c.length < 1) c.length = 1;
                if (c.start < 0) c.start = 0;
                if (c.fade_in < 0 || c.fade_in > c.length) c.fade_in = 0;
                if (c.fade_out < 0 || c.fade_in + c.fade_out > c.length) c.fade_out = 0;
                p->clips[p->nclips++] = c;
            } else if (ok && p->nclips < MAX_CLIPS) {
                long fr = p->samples[c.sample].frames;
                if (c.offset < 0) c.offset = 0;
                if (c.offset > fr - 1) c.offset = fr - 1;
                if (c.offset + c.length > fr) c.length = fr - c.offset;
                if (c.length < 1) c.length = 1;
                if (c.start < 0) c.start = 0;
                if (c.fade_in < 0 || c.fade_in > c.length) c.fade_in = 0;
                if (c.fade_out < 0 || c.fade_in + c.fade_out > c.length) c.fade_out = 0;
                p->clips[p->nclips++] = c;
            }
        } else if (!strcmp(word, "section")) {
            Section sc;
            memset(&sc, 0, sizeof sc);
            int nm = 0;
            ok = sscanf(line, "section %d %d %n", &sc.bar, &sc.bars, &nm) >= 2 && sc.bar >= 0 && sc.bars >= 1 &&
                 sc.bar < 100000 && sc.bars < 100000;
            if (ok && p->nsections < MAX_SECTIONS) {
                if (nm > 0) snprintf(sc.name, sizeof sc.name, "%.23s", line + nm);
                sc.name[strcspn(sc.name, "\r\n")] = 0;
                p->sections[p->nsections++] = sc;
            }
        } else if (!strcmp(word, "bend")) {
            Bend b;
            memset(&b, 0, sizeof b);
            ok = sscanf(line, "bend %d %d", &b.id, &b.keep_time) == 2 && b.id > 0 && !bend_find_c(p, b.id);
            if (ok && p->nbends < MAX_BENDS) p->bends[p->nbends++] = b;
        } else if (!strcmp(word, "bpt")) {
            float at, st;
            ok = sscanf(line, "bpt %f %f", &at, &st) == 2 && p->nbends > 0 && at >= 0 && at < 1e6 && st >= -48 && st <= 48;
            Bend *b = ok ? &p->bends[p->nbends - 1] : NULL;
            if (b && b->n < BEND_POINTS && (b->n == 0 || at >= b->at[b->n - 1])) {
                b->at[b->n] = at;
                b->semis[b->n++] = st;
            }
        } else if (!strcmp(word, "tags")) {
            snprintf(p->tags, sizeof p->tags, "%.127s", line[4] ? line + 5 : "");
            p->tags[strcspn(p->tags, "\r\n")] = 0;
        } else if (!strcmp(word, "scene")) {
            int i = -1, nm = 0;
            ok = sscanf(line, "scene %d %n", &i, &nm) >= 1 && i >= 0 && i < MAX_SCENES;
            if (ok) {
                snprintf(p->scene_names[i], sizeof p->scene_names[i], "%.23s", nm > 0 ? line + nm : "");
                p->scene_names[i][strcspn(p->scene_names[i], "\r\n")] = 0;
                if (i + 1 > p->nscenes) p->nscenes = i + 1;
            }
        } else if (!strcmp(word, "slot")) {
            int t = -1, i = -1, nm = 0;
            Slot sl;
            memset(&sl, 0, sizeof sl);
            Clip *c = &sl.clip;
            ok = sscanf(line, "slot %d %d %d %lf %d %d %d %d %ld %ld %f %d %n", &t, &i, &sl.kind, &sl.beats, &sl.bar,
                        &c->sample, &c->pattern, &c->root, &c->offset, &c->length, &c->gain_db, &c->bend, &nm) >= 12 &&
                 t >= 0 && t < MAX_TRACKS && i >= 0 && i < MAX_SCENES && sl.kind > SLOT_EMPTY && sl.kind <= SLOT_RANGE &&
                 sl.beats > 0 && sl.beats < 100000 && sl.bar >= 0 && c->sample >= 0 && c->sample < p->nsamples;
            if (ok && c->pattern < 0 && !drum_find(p, -c->pattern)) ok = 0;
            if (ok) {
                if (nm > 0) snprintf(sl.name, sizeof sl.name, "%.23s", line + nm);
                sl.name[strcspn(sl.name, "\r\n")] = 0;
                if (c->offset < 0) c->offset = 0;
                p->slots[t][i] = sl;
                if (i + 1 > p->nscenes) p->nscenes = i + 1;
            }
        } else if (!strcmp(word, "key")) {
            int r = 0, sc = 0;
            ok = sscanf(line, "key %d %d", &r, &sc) == 2 && r >= 0 && r < 12 && sc >= 0 && sc < NSCALES;
            if (ok) {
                p->key_root = r;
                p->key_scale = sc;
            }
        } else if (!strcmp(word, "keychange")) {
            KeyChange k;
            ok = sscanf(line, "keychange %d %d %d", &k.bar, &k.root, &k.scale) == 3 && k.bar >= 1 && k.bar < 100000 &&
                 k.root >= 0 && k.root < 12 && k.scale >= 0 && k.scale < NSCALES;
            if (ok && p->nkeys < MAX_KEYS) p->keys[p->nkeys++] = k;
        } else if (!strcmp(word, "harmony")) {
            SongChord c;
            ok = sscanf(line, "harmony %lf %lf %d %d %d", &c.at, &c.len, &c.root, &c.type, &c.bass) == 5 && c.at >= 0 &&
                 c.at < 1e6 && c.len > 0 && c.len < 1e5 && c.root >= 0 && c.root < 12 && c.type >= 0 && c.type < CH_COUNT &&
                 c.bass >= -1 && c.bass < 12;
            if (ok && p->nchords < MAX_CHORDS) p->chords[p->nchords++] = c;
        } else if (!strcmp(word, "drum")) {
            DrumPat d;
            memset(&d, 0, sizeof d);
            int nm = 0;
            ok = sscanf(line, "drum %d %d %d %d %f %n", &d.id, &d.track, &d.steps, &d.nrows, &d.swing, &nm) >= 5 && d.id > 0 &&
                 p->ndrums < MAX_DRUMS && !drum_find(p, d.id);
            if (ok) {
                if (nm > 0) snprintf(d.name, sizeof d.name, "%.23s", line + nm);
                d.name[strcspn(d.name, "\r\n")] = 0;
                d.track = d.track < 0 ? 0 : d.track >= MAX_TRACKS ? MAX_TRACKS - 1 : d.track;
                d.steps = d.steps <= 16 ? 16 : 32;
                d.nrows = d.nrows < 0 ? 0 : d.nrows > DRUM_ROWS ? DRUM_ROWS : d.nrows;
                d.swing = d.swing < 0 ? 0 : d.swing > 0.5f ? 0.5f : d.swing;
                for (int r = 0; r < DRUM_ROWS; r++) {
                    d.sample[r] = -1;
                    d.row_vol[r] = 1;
                }
                p->drums[p->ndrums++] = d;
            }
        } else if (!strcmp(word, "drow") && p->ndrums > 0) {
            DrumPat *d = &p->drums[p->ndrums - 1];
            int r = -1, smp = -1, at = 0;
            float vol = 1;
            if (sscanf(line, "drow %d %d %f %n", &r, &smp, &vol, &at) >= 3 && r >= 0 && r < d->nrows) {
                d->sample[r] = smp >= 0 && smp < p->nsamples ? smp : -1;
                d->row_vol[r] = vol < 0 ? 0 : vol > 2 ? 2 : vol;
                const char *q = line + at;
                for (int k = 0; k < d->steps && *q; k++) {
                    int v = 0, pr = 100, used = 0;
                    if (sscanf(q, " %d:%d%n", &v, &pr, &used) < 2) break;
                    d->vel[r][k] = (unsigned char)(v < 0 ? 0 : v > 127 ? 127 : v);
                    d->prob[r][k] = (unsigned char)(pr < 1 ? 1 : pr > 100 ? 100 : pr);
                    q += used;
                }
            }
        } else if (!strcmp(word, "note")) {
            Note n = {0};
            ok = sscanf(line, "note %d %d %ld %ld %f", &n.pattern, &n.note, &n.start, &n.length, &n.vel) == 5 &&
                 n.pattern > 0 && n.note >= 0 && n.note < 128;
            if (ok && p->nnotes < MAX_NOTES) {
                if (n.start < 0) n.start = 0;
                if (n.length < 1) n.length = 1;
                n.vel = n.vel < 0 ? 0 : n.vel > 1 ? 1 : n.vel;
                p->notes[p->nnotes++] = n;
            }
        } else if (!strcmp(word, "auto")) {
            AutoPoint a = {0};
            ok = sscanf(line, "auto %d %d %ld %f", &a.track, &a.param, &a.at, &a.v) == 4 && a.track >= 0 &&
                 a.track < MAX_TRACKS && a.param >= 0 && a.param < AUTO_PARAMS;
            if (ok && p->nautos < MAX_AUTO) {
                if (a.at < 0) a.at = 0;
                a.v = a.v < 0 ? 0 : a.v > 1 ? 1 : a.v;
                p->autos[p->nautos++] = a;
            }
        } else if (!strcmp(word, "pad")) {
            int i = -1;
            Pad pd;
            memset(&pd, 0, sizeof pd);
            ok = sscanf(line, "pad %d %d %d %f %d %d %f", &i, &pd.sample, &pd.mode, &pd.volume, &pd.pitch, &pd.track,
                        &pd.pan) >= 6 &&
                 i >= 0 && i < NPADS;
            if (ok) {
                if (pd.sample >= p->nsamples) pd.sample = -1;
                if (pd.mode < 0 || pd.mode >= PAD_MODES) pd.mode = PAD_ONESHOT;
                if (pd.track < 0 || pd.track >= MAX_TRACKS) pd.track = 0;
                p->pads[i] = pd;
            }
        }
        if (!ok) {
            snprintf(msg, msglen, "%s line %d is damaged: %.60s", path, lineno, line);
            goto fail;
        }
    }
    fclose(f);
    f = NULL;
    p->ntracks = ntracks > 0 ? ntracks : DEFAULT_TRACKS;
    for (int i = 0; i < p->nsamples; i++) {      /* layers that didn't come back: the sound alone */
        Sample *o = &p->samples[i];
        int good = o->nvel > 0;
        for (int l = 0; good && l < MAX_VEL; l++)
            for (int r = 0; good && r < MAX_RR; r++) good = o->layer[l][r] >= 0 && o->layer[l][r] < p->nsamples;
        if (!good) o->nvel = o->nrr = 0;
    }
    auto_sort(p);
    for (int c = 0; c < p->nclips; c++)          /* a curve that didn't come back: the clip plays straight */
        if (p->clips[c].bend && !bend_find_c(p, p->clips[c].bend)) p->clips[c].bend = 0;
    for (int t = 0; t < MAX_TRACKS; t++)
        for (int i = 0; i < MAX_SCENES; i++)
            if (p->slots[t][i].clip.bend && !bend_find_c(p, p->slots[t][i].clip.bend)) p->slots[t][i].clip.bend = 0;
    keys_tidy(p);
    chords_sort(p);
    for (int i = 0; i + 1 < p->nchords; i++)     /* never overlapping */
        if (p->chords[i].at + p->chords[i].len > p->chords[i + 1].at) p->chords[i].len = p->chords[i + 1].at - p->chords[i].at;
    for (int i = p->nchords - 1; i >= 0; i--)
        if (p->chords[i].len <= 1e-6) chord_remove(p, i);
    for (int c = 0; c < p->nclips; c++)
        if (p->clips[c].track >= p->ntracks) p->ntracks = p->clips[c].track + 1;
    if (p->inst_sample >= p->nsamples) p->inst_sample = -1;
    if (p->inst_track < 0 || p->inst_track >= p->ntracks) p->inst_track = 0;
    if (p->fx.sc_source >= p->ntracks) p->fx.sc_source = -1;
    if (p->loop_end < p->loop_start) p->loop_end = p->loop_start;
    p->playing = 0;
    p->preview = -1;
    const char *base = path_base(path);
    int listed = 0;                              /* the sounds the library shows (not layers or copies) */
    for (int i = 0; i < p->nsamples; i++)
        listed += !p->samples[i].hidden && !p->samples[i].removed && (!p->samples[i].pitched || p->samples[i].listed);
    int n = snprintf(msg, msglen, "Opened %s (%d clips, %d sounds)", base, p->nclips, listed);
    if (missing && n > 0 && (size_t)n < msglen)
        n += snprintf(msg + n, msglen - n, " - %d audio file%s missing, playing as silence", missing, missing == 1 ? "" : "s");
    if (unsung && n > 0 && (size_t)n < msglen)
        snprintf(msg + n, msglen - n, " - %d sung part%s silent: install eSpeak NG to hear %s", unsung,
                 unsung == 1 ? "" : "s", unsung == 1 ? "it" : "them");
    return 0;

fail:
    if (f) fclose(f);
    project_free(p);
    g_rate = old_rate;
    return -1;
}

/* ---- the project browser's quick look */

int project_peek(const char *path, ProjectInfo *o)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    memset(o, 0, sizeof *o);
    o->bpm = 120;
    o->ts_num = o->ts_den = 4;
    o->rate = 48000;
    char line[2048];
    int ok = fgets(line, sizeof line, f) && !strncmp(line, MAGIC, strlen(MAGIC) - 1);
    long end = 0, bar16 = 0;
    int in_sample = 0;
    while (ok && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char word[16] = "";
        sscanf(line, "%15s", word);
        if (!strcmp(word, "sample")) in_sample = 1;
        else if (!strcmp(word, "end")) in_sample = 0;
        if (in_sample) continue;
        if (!strcmp(word, "rate")) sscanf(line, "rate %d", &o->rate);
        else if (!strcmp(word, "bpm")) sscanf(line, "bpm %lf", &o->bpm);
        else if (!strcmp(word, "meter")) sscanf(line, "meter %d %d", &o->ts_num, &o->ts_den);
        else if (!strcmp(word, "title")) snprintf(o->title, sizeof o->title, "%.95s", line + 6);
        else if (!strcmp(word, "tags")) snprintf(o->tags, sizeof o->tags, "%.127s", line[4] ? line + 5 : "");
        else if (!strcmp(word, "key")) o->has_key = sscanf(line, "key %d %d", &o->key_root, &o->key_scale) == 2;
        else if (!strcmp(word, "track")) o->ntracks++;
        else if (!strcmp(word, "harmony")) o->nchords++;
        else if (!strcmp(word, "scene")) o->nscenes++;
        else if (!strcmp(word, "section")) {
            int b, n, nm = 0;
            if (sscanf(line, "section %d %d %n", &b, &n, &nm) >= 2 && o->npsec < 16) {
                o->sections[o->npsec].bar = b;
                o->sections[o->npsec].bars = n;
                snprintf(o->sections[o->npsec].name, sizeof o->sections[0].name, "%.23s", nm > 0 ? line + nm : "");
                o->npsec++;
            }
            o->nsections++;
        } else if (!strcmp(word, "clip")) {
            int sm, tr;
            long st, off, len;
            if (sscanf(line, "clip %d %d %ld %ld %ld", &sm, &tr, &st, &off, &len) != 5) continue;
            o->nclips++;
            if (st + len > end) end = st + len;
            if (!bar16) bar16 = (long)(16.0 * o->ts_num * 4.0 / o->ts_den * 60.0 / (o->bpm > 0 ? o->bpm : 120) * o->rate);
            if (st < bar16 && o->npeek < PEEK_CLIPS) {
                o->clips[o->npeek].track = (short)tr;
                o->clips[o->npeek].start = st;
                o->clips[o->npeek].length = len;
                o->npeek++;
            }
        }
    }
    fclose(f);
    if (!ok) return -1;
    o->seconds = o->rate > 0 ? (double)end / o->rate : 0;
    if (o->nchords) o->has_key = 1;               /* chords and no key line: C major, the one that isn't written */
    return 0;
}

int project_set_tags(const char *path, const char *tags)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    buf[n] = 0;
    if (strncmp(buf, MAGIC, strlen(MAGIC) - 1)) {
        free(buf);
        return -1;
    }
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tags-tmp", path);
    FILE *o = fopen(tmp, "wb");
    if (!o) {
        free(buf);
        return -1;
    }
    int wrote = 0;
    char *p = buf;
    while (*p) {                                 /* line by line: the old tags line goes, the new one goes in once */
        char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p + 1) : strlen(p);
        int is_tags = !strncmp(p, "tags ", 5) || !strncmp(p, "tags\n", 5) || !strncmp(p, "tags\r", 5);
        int is_track = !strncmp(p, "track ", 6);
        if (!wrote && (is_tags || is_track)) {   /* before the first track, if there was no tags line */
            if (tags[0]) fprintf(o, "tags %s\n", tags);
            wrote = 1;
        }
        if (!is_tags) fwrite(p, 1, len, o);
        p += len;
    }
    if (!wrote && tags[0]) fprintf(o, "tags %s\n", tags);
    free(buf);
    if (fclose(o) != 0) {
        remove(tmp);
        return -1;
    }
    return replace_file(tmp, path);
}
