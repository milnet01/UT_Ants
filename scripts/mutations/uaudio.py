"""uaudio's mutations, for scripts/mutation-probe.py -- UTA-0056's rules:
the WAV decoder and the mixer. One or more per invariant from INV-1 to
INV-9, each one an invariant's *Breaks when:* (spec SS 7).

Each entry: (invariant, label, project-relative file, search text,
replacement), as ubundle.py's.
"""

WAV = "src/uaudio/Wav.cpp"
MIXER = "src/uaudio/Mixer.cpp"

SUBJECT = {
    "spec": "docs/specs/UTA-0056-audio-mixer.md",
    "target": "uta_unit_tests",
    "binary": "tests/uta_unit_tests",
    "filter": "[uaudio]",
    "mutations": [
        ("INV-1", "the 8-bit bias dropped", WAV,
         "(static_cast<float>(std::to_integer<unsigned>((*data)[i])) - 128.0f) / 128.0f",
         "static_cast<float>(std::to_integer<unsigned>((*data)[i])) / 128.0f"),
        ("INV-1", "a tag-17 payload read as PCM", WAV,
         "if (format->tag != FORMAT_PCM)", "if (false)"),
        ("INV-1", "a chunk past the end read", WAV,
         "if (size > bytes.size() - body)", "if (false)"),
        ("INV-2", "the step from the frame count", MIXER,
         ".step = static_cast<double>(clip.rate) / RATE",
         ".step = static_cast<double>(clip.frames()) / RATE"),
        ("INV-2", "interpolation past the clip's end", MIXER,
         "const std::size_t next = at + 1 < length ? at + 1 : at;",
         "const std::size_t next = at + 1;"),
        ("INV-3", "a group's volume on the next group", MIXER,
         "factor[g] = settings_.groupVolume[g] *",
         "factor[g] = settings_.groupVolume[(g + 1) % GROUP_COUNT] *"),
        ("INV-3", "the master volume twice", MIXER,
         "* (dip == 0 ? 1.0f : gainOf(dip)) * settings_.master;",
         "* (dip == 0 ? 1.0f : gainOf(dip)) * settings_.master * settings_.master;"),
        ("INV-4", "a weapons voice triggers the dip", MIXER,
         "if (group == Group::Speech) speechUntil",
         "if (group >= Group::Weapons) speechUntil"),
        ("INV-4", "release at the first speech voice's end", MIXER,
         "speechUntil = std::max(speechUntil, played);",
         "speechUntil = speechUntil == 0 ? played : std::min(speechUntil, played);"),
        ("INV-4", "speech ducked too", MIXER,
         "if (g == indexOf(Group::Speech) || !settings_.duck", "if (!settings_.duck"),
        ("INV-4", "the duck switch ignored", MIXER,
         "|| !settings_.duck || release == 0", "|| release == 0"),
        ("INV-5", "the cap counts every clip's voices", MIXER,
         "        if (voice.clip != &clip) continue;\n        ++ofClip;",
         "        if (voice.clip == nullptr) continue;\n        ++ofClip;"),
        ("INV-5", "the newest voice stolen", MIXER,
         "if (oldestOfClip == nullptr || voice.order < oldestOfClip->order)",
         "if (oldestOfClip == nullptr || voice.order > oldestOfClip->order)"),
        ("INV-6", "collapsed gains add linearly", MIXER,
         "std::sqrt(joined->gain * joined->gain + request.gain * request.gain)",
         "joined->gain + request.gain"),
        ("INV-6", "the window a frame short", MIXER,
         "frame_ - voice.start > COLLAPSE_FRAMES", "frame_ - voice.start >= COLLAPSE_FRAMES"),
        ("INV-7", "the oldest stolen regardless of group", MIXER,
         "std::tuple(v.group, v.priority, v.order)", "std::tuple(v.order, v.group, v.priority)"),
        ("INV-7", "stolen from a higher group", MIXER,
         "        if (voice.group > group) continue; // never from a higher group\n", ""),
        ("INV-8", "the limiter removed", MIXER,
         "        out[i * 2] = left * limiterGain_;\n        out[i * 2 + 1] = right * limiterGain_;",
         "        out[i * 2] = left;\n        out[i * 2 + 1] = right;"),
        ("INV-8", "the limiter releases between channels", MIXER,
         "        out[i * 2 + 1] = right * limiterGain_;\n"
         "        limiterGain_ = std::min(1.0f, limiterGain_ + limiterStep_);",
         "        limiterGain_ = std::min(1.0f, limiterGain_ + limiterStep_);\n"
         "        out[i * 2 + 1] = right * limiterGain_;"),
        ("INV-9", "a vector built per call", MIXER,
         "    const std::size_t frames = out.size() / 2;\n",
         "    const std::size_t frames = out.size() / 2;\n"
         "    std::vector<float> spare(frames + 1, 1.0f);\n"
         "    out[0] *= spare[frames];\n"),
    ],
    "expected_survivors": {},
}
