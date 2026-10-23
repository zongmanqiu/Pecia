// FontDialog.cpp - system font picker (see FontDialog.h)
#include "ui/FontDialog.h"

#include "core/Config.h"
#include "core/I18n.h"
#include "core/Theme.h"
#include "editor/FontUtils.h"    // FontUtils: enumerate / lookup / monospace
#include "ui/BlinkInput.h"       // 与主编辑器同周期闪烁的输入框
#include "ui/Layout.h"           // gBarH / gBtnH / TITLE_H
#include "ui/ThemeWidgets.h"     // HoverButton / createDialogButtonBar / drawToolScrollbar

#include <FL/Fl_Box.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Button.H>
#include <FL/fl_draw.H>

#include <cctype>
#include <cstring>

static const int MARGIN  = 16;
static const int GAP     = 8;
static const int ROW_H   = 22;
static const int SEARCH_H = 24;   // 与 Options 的搜索引擎输入框同高
static const int LIST_H  = 240;
static const int PREV_H  = 90;    // three sample lines: Latin / UI language / CJK
static const int WIN_W   = 540;

// Three fixed sample lines: Latin metrics (with digits), CJK metrics (no
// digits - the Latin line already covers them), and the current UI language's
// tagline (`about.tagline`, translated in every language file). The lines use
// spaces as separators, matching the Latin sample.
static const char *kSampleBig   = "ABC abc 0123";
static const char *kSampleSmall = "中文示例 汉字 字体";

// Substring match, ASCII case-insensitive (same rule as FontUtils).
static bool containsCI(const std::string &hay, const char *needle) {
    if (!needle || !*needle) return true;
    const char *h = hay.c_str();
    for (const char *p = h; *p; ++p) {
        const char *a = p, *b = needle;
        while (*a && *b) {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) break;
            ++a; ++b;
        }
        if (!*b) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// FontBrowser - an Fl_Browser whose rows are drawn with the very font they
// name, so the list itself is the preview (a font picker without this makes
// you confirm every candidate one at a time).
// ---------------------------------------------------------------------------
class FontDialog::FontBrowser : public Fl_Browser {
public:
    FontBrowser(int X, int Y, int W, int H, const ThemeColors &tc)
        : Fl_Browser(X, Y, W, H), m_tc(tc) {
        type(FL_HOLD_BROWSER);        // click selects; no callback-driven editing
        box(FL_BORDER_BOX);
        has_scrollbar(Fl_Browser_::VERTICAL);
        // 滚动条与主编辑器同款（10px、扁平、轨道 background1 / 滑块 background2）：
        // FLTK 默认滚动条带凹槽+箭头，摆在这个对话框里跟主窗口明显不是一套控件。
        scrollbar_size(10);
        styleToolScrollbar(&scrollbar, FL_VERT_SLIDER, m_tc);
    }
    void setMetrics(int rowH, Fl_Color text, Fl_Color sel) {
        m_rowH = rowH; m_text = text; m_sel = sel;
    }

    void draw() FL_OVERRIDE {
        Fl_Browser::draw();
        // Fl_Browser 先用它自己的方式画过滚动条，这里用与主编辑器逐像素相同的
        // 画法覆盖一遍（SidePanel 也是这么做的）。
        drawToolScrollbar(&scrollbar, false, m_tc);
    }

    int item_height(void *) const FL_OVERRIDE { return m_rowH; }

    int item_width(void *item) const FL_OVERRIDE {
        const char *t = item_text(item);
        if (!t || !*t) return 8;
        fl_font(FontUtils::fontNameToId(t), m_rowH - 6);
        return (int)fl_width(t) + 12;
    }

    void item_draw(void *item, int X, int Y, int W, int H) const FL_OVERRIDE {
        const char *t = item_text(item);
        if (!t || !*t) return;
        if (item_selected(item)) {
            fl_color(m_sel);
            fl_rectf(X, Y, W, H);
        }
        fl_font(FontUtils::fontNameToId(t), m_rowH - 6);
        fl_color(m_text);
        // Clip: some faces are much wider than the list and would otherwise
        // paint straight over the scrollbar.
        fl_draw(t, X + 6, Y, W - 8, H,
                (Fl_Align)(FL_ALIGN_LEFT | FL_ALIGN_CLIP));
    }

private:
    int            m_rowH = ROW_H;
    Fl_Color       m_text = FL_BLACK;
    Fl_Color       m_sel  = FL_SELECTION_COLOR;
    ThemeColors    m_tc;                // 按值存：允许调用方传临时对象（无主题时）
};

// ---------------------------------------------------------------------------
// PreviewBox - sample text in the selected face.
// ---------------------------------------------------------------------------
class FontDialog::PreviewBox : public Fl_Box {
public:
    PreviewBox(int X, int Y, int W, int H) : Fl_Box(X, Y, W, H) {
        box(FL_BORDER_BOX);
    }
    void setSampleFont(Fl_Font f) { m_font = f; redraw(); }
    void setTextColor(Fl_Color c) { m_text = c; redraw(); }

    void draw() FL_OVERRIDE {
        // Flat fill first (FL_BORDER_BOX only strokes the frame), then the
        // frame, then the samples.
        fl_color(color());
        fl_rectf(x(), y(), w(), h());
        draw_box();
        int tx = x() + 10;
        int tw = w() - 20;
        fl_color(m_text);
        // All three lines share one size so the preview never looks like it
        // switched typefaces between lines (16 vs 13 used to read that way).
        fl_font(m_font, 15);
        fl_draw(kSampleBig, tx, y() + 8, tw, 22,
                (Fl_Align)(FL_ALIGN_LEFT | FL_ALIGN_CLIP));
        fl_draw(kSampleSmall, tx, y() + 32, tw, 22,
                (Fl_Align)(FL_ALIGN_LEFT | FL_ALIGN_CLIP));
        const char *tagline = I18n::get("about.tagline");
        if (tagline && *tagline) {
            fl_draw(tagline, tx, y() + 56, tw, 22,
                    (Fl_Align)(FL_ALIGN_LEFT | FL_ALIGN_CLIP));
        }
    }

private:
    Fl_Font  m_font = FL_HELVETICA;
    Fl_Color m_text = FL_BLACK;
};

// ---------------------------------------------------------------------------
// FontDialog
// ---------------------------------------------------------------------------
FontDialog::FontDialog(const Theme *theme, int uiFontSize, const char *currentFont)
    : DialogBase(WIN_W, 520, I18n::get("fontdialog.title"), theme, uiFontSize,
                 ModalDialog)
    , m_current(currentFont ? currentFont : "") {
    const ThemeColors *tc = theme ? &theme->colors() : nullptr;
    const int fs = uiFontSize ? uiFontSize : 14;
    m_itemSize = fs;

    int pad = 16;
    {
        Config cfg;
        pad = cfg.getDialogPad();
    }

    m_all = FontUtils::systemFontNames();
    m_mono.assign(m_all.size(), -1);

    begin();
    initShell(I18n::get("fontdialog.title"));

    int y = TITLE_H + pad;

    // --- search box -----------------------------------------------------
    // 与 ShortcutDialog 完全同款的搜索框(ui/ThemeWidgets.h 共用组件):
    // 22px 高、左侧 24px 内缩 + 放大镜图标 + 输入内浅色提示字。旧版这里是
    // 全宽 gBarH 高的裸输入框,比快捷键界面的大一圈且没有图标。
    m_search = new SearchInput(MARGIN, y, WIN_W - 2 * MARGIN, SEARCH_H);
    m_search->textsize(fs);
    m_search->labelsize(fs);
    m_search->color(tc ? tc->background1 : FL_WHITE);
    m_search->textcolor(tc ? tc->text1 : FL_BLACK);
    m_search->cursor_color(tc ? tc->text1 : FL_BLACK);
    m_search->selection_color(tc ? tc->highlight2 : FL_SELECTION_COLOR);
    m_search->when(FL_WHEN_CHANGED | FL_WHEN_RELEASE);
    m_search->callback(cbSearch, this);
    m_search->iconColor(tc ? tc->text2 : fl_rgb_color(140, 140, 140));

    // 输入内浅色提示字(复用已有的 fontdialog.search 键,15 个语言文件都有):
    // 放在图标内缩之后,显隐规则与快捷键对话框同一套(updateSearchHint)。
    m_searchHint = new Fl_Box(FL_NO_BOX, m_search->x() + kSearchIconW + 4,
                              m_search->y(),
                              m_search->w() - kSearchIconW - 8, SEARCH_H,
                              I18n::get("fontdialog.search"));
    m_searchHint->labelsize(fs);
    m_searchHint->labelcolor(tc ? tc->text2 : fl_rgb_color(140, 140, 140));
    m_searchHint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    y += SEARCH_H + GAP;

    // --- monospace-only switch -----------------------------------------
    m_monoBox = new Fl_Check_Button(MARGIN, y, WIN_W - 2 * MARGIN, ROW_H,
                                    I18n::get("fontdialog.mono"));
    m_monoBox->box(FL_NO_BOX);
    m_monoBox->down_box(FL_BORDER_BOX);
    m_monoBox->labelsize(fs);
    m_monoBox->labelcolor(tc ? tc->text1 : FL_BLACK);
    m_monoBox->color(tc ? tc->background1 : FL_WHITE);
    m_monoBox->selection_color(tc ? tc->highlight2 : FL_SELECTION_COLOR);
    m_monoBox->callback(cbMono, this);
    y += ROW_H + GAP;

    // --- the list -------------------------------------------------------
    m_list = new FontBrowser(MARGIN, y, WIN_W - 2 * MARGIN, LIST_H,
                             tc ? *tc : ThemeColors());
    m_list->setMetrics(ROW_H + 4,
                       tc ? tc->text1 : FL_BLACK,
                       tc ? tc->highlight2 : FL_SELECTION_COLOR);
    m_list->color(tc ? tc->background1 : FL_WHITE);
    m_list->textcolor(tc ? tc->text1 : FL_BLACK);
    m_list->callback(cbList, this);
    m_list->when(FL_WHEN_RELEASE);
    y += LIST_H + GAP;

    // --- preview --------------------------------------------------------
    m_preview = new PreviewBox(MARGIN, y, WIN_W - 2 * MARGIN, PREV_H);
    m_preview->color(tc ? tc->background1 : FL_WHITE);
    m_preview->setTextColor(tc ? tc->text1 : FL_BLACK);
    y += PREV_H + GAP;

    // --- 底部按钮条 -----------------------------------------------------
    // 与 Options / 确认框 / AI 聊天 / Lua 工具窗完全同一套（ThemeWidgets.h 的
    // createDialogButtonBar）：通栏 background2 底 + 变宽 HoverButton 右对齐。
    // 旧版这里是自己写的：固定 96px 宽、background3 底色、highlight1 悬停、
    // 没有页脚条 —— 一排按钮跟别处明显不是一套控件。
    const int barY = y;
    const int barH = gBarH;
    size(WIN_W, barY + barH);

    HoverButton *btnOk     = new HoverButton(0, 0, 0, gBtnH, I18n::get("dlg.ok"));
    HoverButton *btnCancel = new HoverButton(0, 0, 0, gBtnH, I18n::get("dlg.cancel"));
    btnOk->callback(cbOk, this);
    btnCancel->callback(cbCancel, this);
    btnOk->when(FL_WHEN_RELEASE);
    btnCancel->when(FL_WHEN_RELEASE);
    {
        static ThemeColors kFallback;   // 无主题（极少见）时的中性配色
        // ★ 顺序 = 从左到右的视觉顺序，**取消/否定类永远在最右**。
        //   createDialogButtonBar 内部是"倒序从右往左摆"，所以数组最后一个
        //   落在最右边。这里曾写成 {btnCancel, btnOk}，于是变成
        //   [取消][确定] —— 与全项目其它对话框（Options / 快捷键 / 确认框
        //   都是 [OK][Cancel]）正好相反。
        createDialogButtonBar(0, barY, WIN_W, barH, tc ? *tc : kFallback, fs,
                              {btnOk, btnCancel}, MARGIN, GAP);
    }

    end();
    finalizeShell();

    rebuildList();
    if (!m_shown.empty()) {
        int line = 1;
        for (size_t i = 0; i < m_shown.size(); ++i) {
            if (FontUtils::strCaseCmp(m_shown[i].c_str(), m_current.c_str()) == 0) {
                line = (int)i + 1;
                break;
            }
        }
        m_list->value(line);
        syncSelection();
    }
    Fl::focus(m_search);
    updateSearchHint();   // 焦点已在输入框:空框不显提示字(与快捷键框一致)
}

FontDialog::~FontDialog() = default;

bool FontDialog::runModal() {
    m_chosen.clear();
    // 与其它对话框一致：居中显示。此前没有这一步，窗口开在左上角，
    // 与 Options / 确认框 / 快捷键等窗口的行为明显不同。
    position((Fl::w() - w()) / 2, (Fl::h() - h()) / 2);
    show();
    while (shown()) Fl::wait();
    return !m_chosen.empty();
}

int FontDialog::handle(int event) {
    if (event == FL_SHORTCUT && Fl::event_key() == FL_Escape) {
        m_chosen.clear();
        hide();
        return 1;
    }
    int ret = DialogBase::handle(event);
    // 提示字随焦点/输入显隐(与 ShortcutDialog::handle 同一钩子集)。
    if (event == FL_FOCUS || event == FL_UNFOCUS || event == FL_KEYUP)
        updateSearchHint();
    return ret;
}

bool FontDialog::isMono(size_t idx) {
    if (idx >= m_mono.size()) return false;
    if (m_mono[idx] >= 0) return m_mono[idx] != 0;
    bool v = FontUtils::isMonospace(m_all[idx].c_str());
    m_mono[idx] = v ? 1 : 0;
    return v;
}

// 提示字显隐:输入框为空且未聚焦时显示,否则隐藏(与
// ShortcutDialog::updateHintVisibility 同一规则;这里是静态提示,不复用作
// 录制/错误行)。
void FontDialog::updateSearchHint() {
    if (!m_searchHint || !m_search) return;
    const char *v = m_search->value();
    if (!v || !*v)
        (Fl::focus() == m_search) ? m_searchHint->hide() : m_searchHint->show();
    else
        m_searchHint->hide();
}

void FontDialog::rebuildList() {
    const char *q = m_search ? m_search->value() : "";
    bool monoOnly = m_monoBox && m_monoBox->value();
    m_shown.clear();
    m_list->clear();
    for (size_t i = 0; i < m_all.size(); ++i) {
        if (!containsCI(m_all[i], q)) continue;
        if (monoOnly && !isMono(i)) continue;
        m_shown.push_back(m_all[i]);
        m_list->add(m_all[i].c_str());
    }
    m_list->redraw();
}

void FontDialog::syncSelection() {
    int line = m_list->value();
    if (line < 1 || line > (int)m_shown.size()) return;
    m_preview->setSampleFont(FontUtils::fontNameToId(m_shown[line - 1].c_str()));
}

void FontDialog::selectLine(int line) {
    if (!m_list || line < 1 || line > (int)m_shown.size()) return;
    m_list->value(line);
    syncSelection();
}

void FontDialog::cbSearch(Fl_Widget *, void *data) {
    FontDialog *self = static_cast<FontDialog *>(data);
    if (self) {
        self->rebuildList();
        self->updateSearchHint();
    }
}

void FontDialog::cbMono(Fl_Widget *, void *data) {
    FontDialog *self = static_cast<FontDialog *>(data);
    if (self) self->rebuildList();
}

void FontDialog::cbList(Fl_Widget *, void *data) {
    FontDialog *self = static_cast<FontDialog *>(data);
    if (!self) return;
    self->syncSelection();
    // Double click = accept, same as pressing OK.
    if (Fl::event_clicks()) {
        int line = self->m_list->value();
        if (line >= 1 && line <= (int)self->m_shown.size()) {
            self->m_chosen = self->m_shown[line - 1];
            self->hide();
        }
    }
}

void FontDialog::cbOk(Fl_Widget *, void *data) {
    FontDialog *self = static_cast<FontDialog *>(data);
    if (!self) return;
    int line = self->m_list->value();
    if (line >= 1 && line <= (int)self->m_shown.size())
        self->m_chosen = self->m_shown[line - 1];
    self->hide();
}

void FontDialog::cbCancel(Fl_Widget *, void *data) {
    FontDialog *self = static_cast<FontDialog *>(data);
    if (!self) return;
    self->m_chosen.clear();
    self->hide();
}
