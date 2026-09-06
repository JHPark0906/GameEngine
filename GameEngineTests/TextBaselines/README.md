# Text baselines

These are pictures of glyphs this engine drew, kept so that it can notice when it
starts drawing them differently.

They are **not** a record of what is correct. Nothing here was checked against a
platform renderer or against anyone's judgement of whether the text reads well —
that comparison lives in `GlyphAgainstDirectWriteTests`, and the judgement of
readability belongs to the person looking at the screen. What these files say is
narrower and still worth having: *this is what we drew last time.* A change here
means the rasterizer's output moved, and someone should know whether that was
intended.

## What is stored

One file per (font, character, size), as plain-text PGM. Plain text so that `git
diff` shows the change, and PGM so that any image viewer opens it.

Each file carries its own provenance in the header:

```
P2
# glyph: D2Coding 'A' at 16px
# updated: 2026-09-04
# reason: first baseline
# structure: box 8x11 at (0,-11), ink 31.11, stroke 2x5
```

The `reason` line is the point of the format. A baseline whose picture changed but
whose reason still says "first baseline" is an update that nobody explained, and
that is visible in review without anyone having to compare images.

## When a baseline test goes red

**It means the rasterizer changed.** That is the whole message. It does not say
whether the change is good.

Work out which it is before touching anything here:

1. Read the structural numbers the failure prints. A change in the ink box or
   stroke weight is a different shape; a change only in area is usually a change
   in anti-aliasing.
2. Find the change that caused it. If you cannot, do not update the baseline —
   an unexplained change in glyph rendering is the thing this directory exists to
   catch.
3. If the change was intended, update the baseline **and say why** in the same
   commit.

## Updating

Updates are deliberate, never automatic. Nothing in the test run rewrites these
files; a run with the environment variable set does:

```
GAMEENGINE_UPDATE_TEXT_BASELINES=1
```

That deliberateness is the safeguard. A test that repairs itself when it fails is
a test that reports nothing, and the failure mode is quiet: the picture drifts a
little with each change, every run is green, and nobody ever sees the drift
accumulate.

**Who may update:** whoever made the rasterizer change, in the commit that makes
it. Not a later commit, and not someone tidying a red build — if the build is red
and you did not change the rasterizer, the answer is upstream of you.

**What an update must carry:** the new `reason` line saying what changed and why
it was wanted, and the structural numbers before and after in the commit message.
The numbers matter because "the glyph looks slightly different" is not reviewable
and "stroke weight went from 2x5 to 3x5" is.

## A limitation worth knowing

These are exact pixel comparisons of floating-point rasterization. They hold
across builds of the same compiler on the same architecture, which is the case
this project has. They are not guaranteed to hold if that changes, and if they
ever fail wholesale on an unchanged rasterizer, that is the reason to suspect
before hunting for a defect.
