// preview_gate.h - the markdown preview's "did the content change since the
// last render?" decision, extracted so it can be tested directly.
//
// Why this exists (2026-10-05, task #8):
//   refreshPreview() used to hold a full copy of the last-rendered text
//   (m_lastRenderedMd) and string-compare the document against it, then had a
//   hard cutoff: documents over 4 MB got m_preview->clear() and returned —
//   a silently blank preview panel, indistinguishable from broken markdown.
//
//   The cutoff was not a policy decision; it was a band-aid over the real
//   cost. That comparison meant three whole-document allocations per refresh
//   tick (buffer()->text(), the std::string built from it, and the stored
//   copy) — roughly 150 MB of alloc+copy every 5 seconds on a 50 MB file,
//   on the UI thread. Large previews were unusable *because* the change
//   detection was O(document size), not because rendering big documents is
//   expensive.
//
//   So the fix is the detection, not a limit. This gate keys on
//   (document identity, Document::contentRev()) — O(1), no document bytes
//   touched — and the size cutoff is gone for good: whatever you can open,
//   you can preview. Slow is the user's choice.
//
// Living in its own header (rather than inline in MainWindow_preview.cpp)
// is deliberate: the gate used to be a private field pair plus a comparison
// that no test could reach, which is how the "large files are skipped"
// behaviour survived unchallenged. Now the production decision is the tested
// decision.

#pragma once

// Monotonic identity of the last-rendered content.
//
// Cheap by construction: it holds a pointer and an integer, and the only
// thing it asks of a Document is a counter it already maintains for its own
// line-count cache. It never reads the document's text.
class PreviewRenderGate {
public:
    // True when the preview must be re-rendered, and records the new
    // position. `force` bypasses the unchanged-check (manual Refresh, font
    // or scale changes) but is one-shot: it does not stick, so the next
    // tick falls back to normal change detection.
    //
    // `doc` may be null (no active tab) — treated as its own identity, so
    // closing the preview or having no tab neither crashes nor wedges the
    // gate: switching back to a document still renders.
    bool shouldRender(const void *doc, unsigned long long contentRev, bool force) {
        if (force) m_force = true;
        if (!m_force && doc == m_doc && contentRev == m_rev)
            return false;
        m_force = false;
        m_doc = doc;
        m_rev = contentRev;
        return true;
    }

    // Drop the remembered position so the next shouldRender() renders.
    // Used where the old code cleared the stored text to force output.
    void invalidate() { m_force = true; }

private:
    const void        *m_doc = nullptr;
    unsigned long long m_rev = 0;
    bool               m_force = true;   // start dirty: the first call renders
};
