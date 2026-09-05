// =============================================================================
//  main.cpp - interface Win32 do trainer
// =============================================================================
#include "trainer.h"

#include <commctrl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <vector>
#include <string>

// ------------------------------------------------------------------ paleta ---
static const COLORREF C_BG     = RGB( 18,  20,  26);
static const COLORREF C_PANEL  = RGB( 26,  29,  36);
static const COLORREF C_CARD   = RGB( 33,  37,  46);
static const COLORREF C_CARD2  = RGB( 42,  48,  60);
static const COLORREF C_ACCENT = RGB(  0, 210, 106);
static const COLORREF C_BLUE   = RGB( 56, 189, 248);
static const COLORREF C_MUTED  = RGB(150, 158, 172);
static const COLORREF C_TEXT   = RGB(238, 240, 245);
static const COLORREF C_RED    = RGB(255,  90,  90);

static HBRUSH hbBg, hbPanel, hbCard, hbCard2;
static HFONT  fTitle, fBold, fNorm, fSmall, fMono;

// ------------------------------------------------------------------- ids -----
enum {
    ID_TAB      = 100,
    ID_RESET    = 101,
    ID_PARTY    = 102,
    ID_EDMIRA   = 110,
    ID_EDSEP    = 111,
    ID_EDITEM   = 112,
    ID_CHK_BASE = 1000,
    ID_TRK_BASE = 2000,
    ID_UNI_BASE = 3000,
    ID_DSC_BASE = 4000,
    ID_EDV_BASE = 5000,
    ID_HOT_BASE = 9000
};

static const wchar_t* kTabNames[] = {
    L"  Combate  ", L"  Progressao  ", L"  Itens e Dinheiro  ", L"  Party  "
};
static const int kTabCount = 4;

struct Row {
    int   featureIdx;      // indice em kFeatures
    HWND  chk, trk, ed, unit, dsc;
    bool  checked;
    float value;
    int   hotkey;          // 1..8 => Ctrl+F1..Ctrl+F8, 0 = sem atalho
};
static std::vector<Row> g_rows;

static HWND g_main, g_tab, g_status, g_detail, g_party, g_reset;
static HWND g_edMira, g_edSep, g_edItem, g_lbMira, g_lbSep, g_lbItem;
static int  g_curTab   = 0;
static bool g_updating = false;   // evita laco SetWindowText -> EN_CHANGE

// =============================================================================
//  Utilitarios de desenho
// =============================================================================
static void fillRect(HDC dc, RECT r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void drawText(HDC dc, const wchar_t* s, RECT r, COLORREF c, HFONT f, UINT fmt)
{
    HFONT old = (HFONT)SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, fmt);
    SelectObject(dc, old);
}

// =============================================================================
//  Painel da party (janela filha desenhada a mao)
// =============================================================================
static LRESULT CALLBACK PartyProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        if (rc.right < 1 || rc.bottom < 1) { EndPaint(h, &ps); return 0; }

        HDC     mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HBITMAP ob  = (HBITMAP)SelectObject(mem, bmp);

        fillRect(mem, rc, C_PANEL);

        std::vector<CharaRow> rows = g_engine.partySnapshot();

        RECT hd = { 12, 8, rc.right - 12, 30 };
        drawText(mem, L"Personagens carregados na save (leitura ao vivo)",
                 hd, C_ACCENT, fBold, DT_LEFT | DT_SINGLELINE);

        // %ls e nao %s: no runtime do mingw, %s dentro de um formato wide
        // consome char*, o que truncaria cada rotulo no primeiro caractere.
        wchar_t head[256];
        swprintf(head, 256,
                 L"  %2ls  %-14ls %5ls  %3ls  %5ls/%-5ls  %4ls/%-4ls  %3ls/%-3ls  %9ls",
                 L"#", L"NOME", L"ID", L"LV", L"HP", L"MAX", L"EP", L"MAX",
                 L"CP", L"MAX", L"EXP");

        int y = 40;
        RECT cols = { 10, y, rc.right - 10, y + 20 };
        drawText(mem, head, cols, C_MUTED, fMono, DT_LEFT | DT_SINGLELINE);
        y += 24;

        if (rows.empty()) {
            RECT e = { 12, y + 10, rc.right - 12, y + 70 };
            drawText(mem,
                     L"Nada para mostrar ainda.\n\n"
                     L"Abra o jogo, carregue/inicie uma partida e volte aqui.",
                     e, C_MUTED, fNorm, DT_LEFT | DT_WORDBREAK);
        }

        for (size_t i = 0; i < rows.size() && y < rc.bottom - 24; ++i) {
            const CharaRow& r = rows[i];
            RECT bar = { 10, y, rc.right - 10, y + 22 };
            fillRect(mem, bar, (i & 1) ? C_CARD : C_CARD2);

            wchar_t line[256];
            swprintf(line, 256,
                     L"  %2d  %-14ls %5d  %3d  %5d/%-5d  %4d/%-4d  %3d/%-3d  %9d",
                     r.slot, r.name, r.st.charaId, r.st.level,
                     r.st.hp, r.st.maxHp, r.st.ep, r.st.maxEp,
                     r.st.cp, r.st.maxCp, r.st.exp);

            COLORREF col = C_TEXT;
            if (r.st.hp == 0)               col = C_RED;
            else if (r.st.hp == r.st.maxHp) col = C_ACCENT;
            drawText(mem, line, bar, col, fMono, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            y += 24;
        }

        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// =============================================================================
//  Construcao da interface
// =============================================================================
static HWND mkStatic(HWND parent, const wchar_t* text, int x, int y, int w, int hgt, int id)
{
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | SS_LEFT,
                           x, y, w, hgt, parent, (HMENU)(INT_PTR)id,
                           GetModuleHandleW(nullptr), nullptr);
}

static HWND mkEdit(HWND parent, const wchar_t* text, int x, int y, int w, int id)
{
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                           WS_CHILD | ES_LEFT | ES_AUTOHSCROLL,
                           x, y, w, 22, parent, (HMENU)(INT_PTR)id,
                           GetModuleHandleW(nullptr), nullptr);
}

static void pushFeature(int fi, int& y, int width)
{
    const FeatureInfo& f = kFeatures[fi];
    Row r;
    r.featureIdx = fi;
    r.checked    = false;
    r.hotkey     = 0;
    r.value      = f.slider ? f.def : 0.0f;
    r.trk = r.ed = r.unit = nullptr;

    r.chk = CreateWindowExW(0, L"BUTTON", f.title,
                            WS_CHILD | BS_OWNERDRAW,
                            18, y, width - 270, 24,
                            g_main, (HMENU)(INT_PTR)(ID_CHK_BASE + fi),
                            GetModuleHandleW(nullptr), nullptr);

    if (f.slider) {
        wchar_t buf[32];
        swprintf(buf, 32, L"%.1f", f.def);
        r.ed = mkEdit(g_main, buf, width - 168, y + 1, 66, ID_EDV_BASE + fi);
        SendMessageW(r.ed, WM_SETFONT, (WPARAM)fBold, TRUE);
        SendMessageW(r.ed, EM_SETLIMITTEXT, 8, 0);

        r.unit = mkStatic(g_main, f.unit, width - 96, y + 4, 60, 20, ID_UNI_BASE + fi);
        SendMessageW(r.unit, WM_SETFONT, (WPARAM)fBold, TRUE);

        r.trk = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                WS_CHILD | TBS_HORZ | TBS_NOTICKS,
                                18, y + 26, width - 40, 24,
                                g_main, (HMENU)(INT_PTR)(ID_TRK_BASE + fi),
                                GetModuleHandleW(nullptr), nullptr);
        SendMessageW(r.trk, TBM_SETRANGE, TRUE,
                     MAKELPARAM((int)(f.lo * 10), (int)(f.hi * 10)));
        SendMessageW(r.trk, TBM_SETPOS, TRUE, (LPARAM)(int)(f.def * 10));
        y += 26;
    }

    r.dsc = mkStatic(g_main, f.desc, 42, y + 26, width - 60, 32, ID_DSC_BASE + fi);
    SendMessageW(r.dsc, WM_SETFONT, (WPARAM)fSmall, TRUE);

    y += 54;
    g_rows.push_back(r);
}

static void pushState(const Row& r)
{
    g_engine.setFeature(kFeatures[r.featureIdx].id, r.checked, r.value);
}

// escreve o valor atual no campo numerico (sem disparar EN_CHANGE)
static void editFromValue(Row& r)
{
    if (!r.ed) return;
    wchar_t buf[32];
    swprintf(buf, 32, L"%.1f", r.value);
    g_updating = true;
    SetWindowTextW(r.ed, buf);
    g_updating = false;
}

static void setValue(Row& r, float v, bool fromEdit)
{
    const FeatureInfo& f = kFeatures[r.featureIdx];
    if (v < f.lo) v = f.lo;
    if (v > f.hi) v = f.hi;
    r.value = v;
    if (r.trk) {
        g_updating = true;
        SendMessageW(r.trk, TBM_SETPOS, TRUE, (LPARAM)(int)(v * 10.0f + 0.5f));
        g_updating = false;
    }
    if (!fromEdit) editFromValue(r);
    pushState(r);
}

static void showTab(int tab)
{
    g_curTab = tab;
    for (size_t i = 0; i < g_rows.size(); ++i) {
        int show = (kFeatures[g_rows[i].featureIdx].tab == tab) ? SW_SHOW : SW_HIDE;
        ShowWindow(g_rows[i].chk, show);
        ShowWindow(g_rows[i].dsc, show);
        if (g_rows[i].trk)  ShowWindow(g_rows[i].trk, show);
        if (g_rows[i].ed)   ShowWindow(g_rows[i].ed, show);
        if (g_rows[i].unit) ShowWindow(g_rows[i].unit, show);
    }
    int econ = (tab == 2) ? SW_SHOW : SW_HIDE;
    ShowWindow(g_edMira, econ); ShowWindow(g_lbMira, econ);
    ShowWindow(g_edSep,  econ); ShowWindow(g_lbSep,  econ);
    ShowWindow(g_edItem, econ); ShowWindow(g_lbItem, econ);

    ShowWindow(g_party, (tab == 3) ? SW_SHOW : SW_HIDE);
    InvalidateRect(g_main, nullptr, TRUE);
}

static void readEconomyEdits()
{
    wchar_t buf[32];
    GetWindowTextW(g_edMira, buf, 32);  g_engine.miraValue   = _wtol(buf);
    GetWindowTextW(g_edSep,  buf, 32);  g_engine.sepithValue = _wtol(buf);
    GetWindowTextW(g_edItem, buf, 32);  g_engine.itemValue   = _wtol(buf);
}

// =============================================================================
//  Janela principal
// =============================================================================
static void onCreate(HWND h)
{
    g_main = h;
    RECT rc; GetClientRect(h, &rc);
    const int W = rc.right;

    g_tab = CreateWindowExW(0, WC_TABCONTROLW, L"",
                            WS_CHILD | WS_VISIBLE | TCS_TABS | TCS_FIXEDWIDTH,
                            12, 66, W - 24, 28, h, (HMENU)ID_TAB,
                            GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g_tab, WM_SETFONT, (WPARAM)fBold, TRUE);
    SendMessageW(g_tab, TCM_SETITEMSIZE, 0, MAKELPARAM(150, 24));
    for (int i = 0; i < kTabCount; ++i) {
        TCITEMW it; memset(&it, 0, sizeof(it));
        it.mask = TCIF_TEXT;
        it.pszText = (LPWSTR)kTabNames[i];
        SendMessageW(g_tab, TCM_INSERTITEMW, i, (LPARAM)&it);
    }

    int yByTab[kTabCount];
    for (int i = 0; i < kTabCount; ++i) yByTab[i] = 110;
    for (int i = 0; i < kFeatureCount; ++i)
        pushFeature(i, yByTab[kFeatures[i].tab], W);

    // campos da aba economia
    int y = yByTab[2] + 10;
    g_lbMira = mkStatic(h, L"Mira travada em:", 24, y + 4, 150, 20, 0);
    g_edMira = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"9999999",
                               WS_CHILD | ES_NUMBER, 180, y, 120, 24, h,
                               (HMENU)ID_EDMIRA, GetModuleHandleW(nullptr), nullptr);
    y += 32;
    g_lbSep = mkStatic(h, L"Sepith (cada tipo):", 24, y + 4, 150, 20, 0);
    g_edSep = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"9999",
                              WS_CHILD | ES_NUMBER, 180, y, 120, 24, h,
                              (HMENU)ID_EDSEP, GetModuleHandleW(nullptr), nullptr);
    y += 32;
    g_lbItem = mkStatic(h, L"Qtd. dos itens:", 24, y + 4, 150, 20, 0);
    g_edItem = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"99",
                               WS_CHILD | ES_NUMBER, 180, y, 120, 24, h,
                               (HMENU)ID_EDITEM, GetModuleHandleW(nullptr), nullptr);

    HWND econ[] = { g_lbMira, g_edMira, g_lbSep, g_edSep, g_lbItem, g_edItem };
    for (int i = 0; i < 6; ++i) SendMessageW(econ[i], WM_SETFONT, (WPARAM)fNorm, TRUE);

    g_party = CreateWindowExW(0, L"SoraPartyView", L"", WS_CHILD,
                              12, 104, W - 24, rc.bottom - 104 - 78,
                              h, (HMENU)ID_PARTY, GetModuleHandleW(nullptr), nullptr);

    g_reset = CreateWindowExW(0, L"BUTTON", L"Desativar tudo e restaurar o jogo",
                              WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                              12, rc.bottom - 64, 300, 30, h,
                              (HMENU)ID_RESET, GetModuleHandleW(nullptr), nullptr);

    g_status = mkStatic(h, L"", 12, rc.bottom - 26, W - 24, 20, 0);
    ShowWindow(g_status, SW_SHOW);
    SendMessageW(g_status, WM_SETFONT, (WPARAM)fSmall, TRUE);

    g_detail = mkStatic(h, L"", 330, rc.bottom - 58, W - 342, 20, 0);
    ShowWindow(g_detail, SW_SHOW);
    SendMessageW(g_detail, WM_SETFONT, (WPARAM)fSmall, TRUE);

    showTab(0);

    // teclas de atalho globais: Ctrl+F1..Ctrl+F8 nos toggles simples.
    int hk = 0;
    for (size_t i = 0; i < g_rows.size() && hk < 8; ++i) {
        int fi = g_rows[i].featureIdx;
        if (kFeatures[fi].slider) continue;
        if (RegisterHotKey(h, ID_HOT_BASE + fi, MOD_CONTROL, VK_F1 + hk))
            g_rows[i].hotkey = hk + 1;
        ++hk;
    }
    SetTimer(h, 1, 250, nullptr);
}

static void drawHeader(HDC dc, RECT rc)
{
    RECT top = rc; top.bottom = 60;
    fillRect(dc, top, C_BG);

    RECT t = { 20, 12, rc.right - 360, 34 };
    drawText(dc, L"TRAILS IN THE SKY - 2nd CHAPTER (DEMO)  -  TRAINER",
             t, C_TEXT, fTitle, DT_LEFT | DT_SINGLELINE);

    RECT s = { 20, 34, rc.right - 360, 54 };
    drawText(dc, L"Trainer externo - hooks em code cave + escrita direta na savedata",
             s, C_MUTED, fSmall, DT_LEFT | DT_SINGLELINE);

    std::wstring st = g_engine.statusLine();
    bool ok = g_engine.connected();
    RECT b = { rc.right - 350, 14, rc.right - 16, 42 };
    fillRect(dc, b, ok ? RGB(20, 56, 37) : RGB(43, 26, 29));
    RECT bt = b; bt.left += 10;
    drawText(dc, st.c_str(), bt, ok ? C_ACCENT : C_RED, fSmall,
             DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}

static void drawCards(HDC dc, RECT rc)
{
    RECT panel = { 12, 94, rc.right - 12, rc.bottom - 72 };
    fillRect(dc, panel, C_PANEL);

    if (g_curTab == 3) return;

    for (size_t i = 0; i < g_rows.size(); ++i) {
        const Row& r = g_rows[i];
        if (kFeatures[r.featureIdx].tab != g_curTab) continue;
        RECT wr; GetWindowRect(r.chk, &wr);
        MapWindowPoints(nullptr, g_main, (POINT*)&wr, 2);
        RECT card = { 16, wr.top - 8, rc.right - 28,
                      wr.top + (kFeatures[r.featureIdx].slider ? 78 : 46) };
        fillRect(dc, card, C_CARD);
    }
}

static void drawCheckbox(LPDRAWITEMSTRUCT d)
{
    int fi = (int)d->CtlID - ID_CHK_BASE;
    if (fi < 0 || fi >= kFeatureCount) return;
    Row* row = nullptr;
    for (size_t i = 0; i < g_rows.size(); ++i)
        if (g_rows[i].featureIdx == fi) row = &g_rows[i];
    if (!row) return;

    RECT rc = d->rcItem;
    fillRect(d->hDC, rc, C_CARD);

    RECT box = { rc.left, rc.top + 4, rc.left + 16, rc.top + 20 };
    fillRect(d->hDC, box, row->checked ? C_ACCENT : RGB(60, 66, 80));
    if (row->checked) {
        HPEN pen = CreatePen(PS_SOLID, 2, C_CARD);
        HPEN op  = (HPEN)SelectObject(d->hDC, pen);
        MoveToEx(d->hDC, box.left + 3, box.top + 8, nullptr);
        LineTo  (d->hDC, box.left + 7, box.top + 12);
        LineTo  (d->hDC, box.left + 13, box.top + 4);
        SelectObject(d->hDC, op);
        DeleteObject(pen);
    }

    RECT tr = rc; tr.left += 26;
    drawText(d->hDC, kFeatures[fi].title, tr,
             row->checked ? C_ACCENT : C_TEXT, fBold,
             DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    if (row->hotkey) {
        wchar_t hk[24];
        swprintf(hk, 24, L"Ctrl+F%d", row->hotkey);
        RECT hr = rc; hr.right -= 4;
        drawText(d->hDC, hk, hr, C_MUTED, fSmall,
                 DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }
}

static void drawButton(LPDRAWITEMSTRUCT d)
{
    RECT rc = d->rcItem;
    bool down = (d->itemState & ODS_SELECTED) != 0;
    fillRect(d->hDC, rc, down ? C_CARD : C_CARD2);
    wchar_t txt[128];
    GetWindowTextW(d->hwndItem, txt, 128);
    drawText(d->hDC, txt, rc, C_TEXT, fBold, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
}

static Row* rowByFeature(int fi)
{
    for (size_t i = 0; i < g_rows.size(); ++i)
        if (g_rows[i].featureIdx == fi) return &g_rows[i];
    return nullptr;
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        onCreate(h);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        if (rc.right < 1 || rc.bottom < 1) { EndPaint(h, &ps); return 0; }

        HDC     mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HBITMAP ob  = (HBITMAP)SelectObject(mem, bmp);

        fillRect(mem, rc, C_BG);
        drawHeader(mem, rc);
        drawCards(mem, rc);

        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC  dc  = (HDC)w;
        HWND ctl = (HWND)l;
        int  id  = GetDlgCtrlID(ctl);
        SetBkMode(dc, TRANSPARENT);

        if (ctl == g_status || ctl == g_detail) {
            SetTextColor(dc, ctl == g_status ? C_MUTED : C_BLUE);
            return (LRESULT)hbBg;
        }
        if (id >= ID_DSC_BASE) { SetTextColor(dc, C_MUTED); return (LRESULT)hbCard; }
        if (id >= ID_UNI_BASE) { SetTextColor(dc, C_BLUE);  return (LRESULT)hbCard; }
        if (id >= ID_TRK_BASE) {                            return (LRESULT)hbCard; }
        SetTextColor(dc, C_TEXT);
        return (LRESULT)hbPanel;
    }

    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)w, C_CARD2);
        SetTextColor((HDC)w, C_ACCENT);
        return (LRESULT)hbCard2;

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT d = (LPDRAWITEMSTRUCT)l;
        if (d->CtlID == ID_RESET) drawButton(d);
        else                      drawCheckbox(d);
        return TRUE;
    }

    case WM_NOTIFY: {
        LPNMHDR n = (LPNMHDR)l;
        if (n->idFrom == ID_TAB && n->code == TCN_SELCHANGE)
            showTab((int)SendMessageW(g_tab, TCM_GETCURSEL, 0, 0));
        return 0;
    }

    case WM_HSCROLL: {
        if (g_updating) return 0;
        HWND ctl = (HWND)l;
        for (size_t i = 0; i < g_rows.size(); ++i) {
            if (g_rows[i].trk == ctl) {
                int pos = (int)SendMessageW(ctl, TBM_GETPOS, 0, 0);
                setValue(g_rows[i], pos / 10.0f, false);
                break;
            }
        }
        return 0;
    }

    case WM_HOTKEY: {
        Row* r = rowByFeature((int)w - ID_HOT_BASE);
        if (r) {
            r->checked = !r->checked;
            pushState(*r);
            InvalidateRect(r->chk, nullptr, TRUE);
        }
        return 0;
    }

    case WM_COMMAND: {
        int id   = LOWORD(w);
        int code = HIWORD(w);

        if (id == ID_RESET && code == BN_CLICKED) {
            for (size_t i = 0; i < g_rows.size(); ++i) {
                g_rows[i].checked = false;
                pushState(g_rows[i]);
                InvalidateRect(g_rows[i].chk, nullptr, TRUE);
            }
            return 0;
        }
        if (id >= ID_EDV_BASE && id < ID_EDV_BASE + kFeatureCount) {
            Row* r = rowByFeature(id - ID_EDV_BASE);
            if (!r) return 0;
            if (code == EN_CHANGE && !g_updating) {
                wchar_t buf[32];
                GetWindowTextW(r->ed, buf, 32);
                if (buf[0]) setValue(*r, (float)_wtof(buf), true);
            } else if (code == EN_KILLFOCUS) {
                editFromValue(*r);       // normaliza o texto ao sair do campo
            }
            return 0;
        }
        if (id >= ID_CHK_BASE && id < ID_CHK_BASE + kFeatureCount && code == BN_CLICKED) {
            Row* r = rowByFeature(id - ID_CHK_BASE);
            if (r) {
                r->checked = !r->checked;
                pushState(*r);
                InvalidateRect(r->chk, nullptr, TRUE);
            }
            return 0;
        }
        if ((id == ID_EDMIRA || id == ID_EDSEP || id == ID_EDITEM) && code == EN_CHANGE) {
            readEconomyEdits();
            return 0;
        }
        return 0;
    }

    case WM_TIMER: {
        SetWindowTextW(g_status, g_engine.statusLine().c_str());
        SetWindowTextW(g_detail, g_engine.detailLine().c_str());
        RECT rc; GetClientRect(h, &rc);
        RECT hdr = { rc.right - 360, 10, rc.right, 46 };
        InvalidateRect(h, &hdr, FALSE);
        if (g_curTab == 3) InvalidateRect(g_party, nullptr, FALSE);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        KillTimer(h, 1);
        for (int i = 0; i < kFeatureCount; ++i) UnregisterHotKey(h, ID_HOT_BASE + i);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// =============================================================================
int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int)
{
    INITCOMMONCONTROLSEX ic;
    ic.dwSize = sizeof(ic);
    ic.dwICC  = ICC_TAB_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&ic);

    hbBg    = CreateSolidBrush(C_BG);
    hbPanel = CreateSolidBrush(C_PANEL);
    hbCard  = CreateSolidBrush(C_CARD);
    hbCard2 = CreateSolidBrush(C_CARD2);

    fTitle = CreateFontW(-19, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                         CLEARTYPE_QUALITY, 0, L"Segoe UI");
    fBold  = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                         CLEARTYPE_QUALITY, 0, L"Segoe UI");
    fNorm  = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                         CLEARTYPE_QUALITY, 0, L"Segoe UI");
    fSmall = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                         CLEARTYPE_QUALITY, 0, L"Segoe UI");
    fMono  = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                         CLEARTYPE_QUALITY, 0, L"Consolas");

    WNDCLASSEXW pw; memset(&pw, 0, sizeof(pw));
    pw.cbSize        = sizeof(pw);
    pw.lpfnWndProc   = PartyProc;
    pw.hInstance     = hi;
    pw.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    pw.lpszClassName = L"SoraPartyView";
    RegisterClassExW(&pw);

    WNDCLASSEXW wc; memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = L"SoraTrainerWnd";
    RegisterClassExW(&wc);

    g_engine.start();

    RECT want = { 0, 0, 900, 720 };
    DWORD style = (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX);
    AdjustWindowRect(&want, style, FALSE);
    HWND h = CreateWindowExW(0, L"SoraTrainerWnd",
                             L"Trails in the Sky 2nd Chapter (Demo) - Trainer",
                             style, CW_USEDEFAULT, CW_USEDEFAULT,
                             want.right - want.left, want.bottom - want.top,
                             nullptr, nullptr, hi, nullptr);
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_engine.stop();
    return 0;
}
