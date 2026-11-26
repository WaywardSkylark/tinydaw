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
    if (s->is_beat) return "beat";
    if (s->generated && s->nchord) return "chord";
    if (s->generated) return "gen";
    if (s->pitched) return "variant";
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
    fprintf(f, "rate %d\nbpm %.17g\na4 %.17g\nmaster %.9g\n", SR, p->bpm, p->a4, p->master);
    fprintf(f, "loop %d %ld %ld\nplayhead %ld\n", p->loop, p->loop_start, p->loop_end, p->playhead);
    fprintf(f, "instrument %d %d %d\n", p->inst_sample, p->inst_root, p->inst_track);
    const MasterFx *fx = &p->fx;
    fprintf(f, "fx %d %.9g %.9g %.9g %d %.9g %.9g %.9g %d %.9g %.9g\n", fx->delay_div, fx->delay_feedback,
            fx->delay_tone, fx->delay_level, fx->pingpong, fx->reverb_size, fx->reverb_damp, fx->reverb_level,
            fx->sc_source, fx->sc_release, fx->bend_range);
    for (int t = 0; t < p->ntracks; t++) {
        const Track *tr = &p->tracks[t];
        fprintf(f, "track %.9g %d %d %s\n", tr->volume, tr->mute, tr->solo, tr->name);
        fprintf(f, "trackfx %.9g %d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", tr->pan, tr->filter, tr->cutoff,
                tr->reso, tr->eq[0], tr->eq[1], tr->eq[2], tr->drive, tr->delay_send, tr->reverb_send, tr->duck);
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
        if (s->pitched) fprintf(f, "variant %d %d %d\n", s->src, s->semis, s->reversed);
        if (s->path[0]) fprintf(f, "path %s\n", s->path);
        fprintf(f, "end\n");
    }
    for (int c = 0; c < p->nclips; c++) {
        const Clip *cl = &p->clips[c];
        fprintf(f, "clip %d %d %ld %ld %ld %.9g %ld %ld\n", cl->sample, cl->track, cl->start, cl->offset, cl->length,
                cl->gain_db, cl->fade_in, cl->fade_out);
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
    int has_gen, has_beat;
    GenParams gen;
    double gen_bpm;
    int chord[6], nchord;
    BeatSpec beat;
    int mask, src, semis, reversed;
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
    } else if ((!strcmp(s->kind, "pitched") || !strcmp(s->kind, "variant")) && s->src >= 0 && s->src < p->nsamples) {
        idx = sample_variant(p, s->src, s->semis, s->reversed);
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
        } else if (!strcmp(word, "trackfx")) {
            if (ntracks > 0) {
                Track *t = &p->tracks[ntracks - 1];
                ok = sscanf(line, "trackfx %f %d %f %f %f %f %f %f %f %f %f", &t->pan, &t->filter, &t->cutoff, &t->reso,
                            &t->eq[0], &t->eq[1], &t->eq[2], &t->drive, &t->delay_send, &t->reverb_send, &t->duck) == 11;
                if (t->filter < 0 || t->filter >= FILT_TYPES) t->filter = FILT_OFF;
            }
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
        } else if (in_sample && !strcmp(word, "lane")) {
            int l = -1;
            char pat[33] = "";
            ok = sscanf(line, "lane %d %32s", &l, pat) == 2 && l >= 0 && l < DRUM_LANES;
            if (ok) snprintf(s.beat.lanes[l], 33, "%s", strcmp(pat, "-") ? pat : "");
        } else if (in_sample && !strcmp(word, "pitch")) {         /* older files */
            ok = sscanf(line, "pitch %d %d", &s.src, &s.semis) == 2;
        } else if (in_sample && !strcmp(word, "variant")) {
            ok = sscanf(line, "variant %d %d %d", &s.src, &s.semis, &s.reversed) == 3;
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
            ok = sscanf(line, "clip %d %d %ld %ld %ld %f %ld %ld", &c.sample, &c.track, &c.start, &c.offset, &c.length,
                        &c.gain_db, &c.fade_in, &c.fade_out) >= 5 &&
                 c.sample >= 0 && c.sample < p->nsamples && c.track >= 0 && c.track < MAX_TRACKS;
            if (ok && p->nclips < MAX_CLIPS) {
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
    for (int c = 0; c < p->nclips; c++)
        if (p->clips[c].track >= p->ntracks) p->ntracks = p->clips[c].track + 1;
    if (p->inst_sample >= p->nsamples) p->inst_sample = -1;
    if (p->inst_track < 0 || p->inst_track >= p->ntracks) p->inst_track = 0;
    if (p->fx.sc_source >= p->ntracks) p->fx.sc_source = -1;
    if (p->loop_end < p->loop_start) p->loop_end = p->loop_start;
    p->playing = 0;
    p->preview = -1;
    const char *base = path_base(path);
    int n = snprintf(msg, msglen, "Opened %s (%d clips, %d sounds)", base, p->nclips, p->nsamples);
    if (missing && n > 0 && (size_t)n < msglen)
        snprintf(msg + n, msglen - n, " - %d audio file%s missing, playing as silence", missing, missing == 1 ? "" : "s");
    return 0;

fail:
    if (f) fclose(f);
    project_free(p);
    g_rate = old_rate;
    return -1;
}
