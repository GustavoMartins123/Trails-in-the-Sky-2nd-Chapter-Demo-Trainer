// =============================================================================
//  memory.cpp - acesso ao processo, scanner AOB e montador x64
// =============================================================================
#include "trainer.h"

#include <tlhelp32.h>
#include <psapi.h>
#include <cstring>
#include <cstdio>

// -----------------------------------------------------------------------------
// Process
// -----------------------------------------------------------------------------
bool Process::attach(const wchar_t* exeName)
{
    detach();

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    DWORD found = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) { found = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    if (!found) return false;

    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
                           PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, found);
    if (!h) return false;

    // base e tamanho do modulo principal
    HMODULE mods[512]; DWORD needed = 0;
    if (!EnumProcessModules(h, mods, sizeof(mods), &needed) || needed < sizeof(HMODULE)) {
        CloseHandle(h); return false;
    }
    MODULEINFO mi{};
    if (!GetModuleInformation(h, mods[0], &mi, sizeof(mi))) { CloseHandle(h); return false; }

    handle = h;
    pid    = found;
    base   = (uint64_t)mi.lpBaseOfDll;
    size   = (uint32_t)mi.SizeOfImage;
    return true;
}

void Process::detach()
{
    if (handle) CloseHandle(handle);
    handle = nullptr; pid = 0; base = 0; size = 0;
}

bool Process::alive() const
{
    if (!handle) return false;
    DWORD code = 0;
    return GetExitCodeProcess(handle, &code) && code == STILL_ACTIVE;
}

bool Process::read(uint64_t addr, void* dst, size_t n) const
{
    if (!handle || !addr) return false;
    SIZE_T got = 0;
    return ReadProcessMemory(handle, (LPCVOID)addr, dst, n, &got) && got == n;
}

bool Process::write(uint64_t addr, const void* src, size_t n) const
{
    if (!handle || !addr) return false;
    SIZE_T put = 0;

    // A maioria das escritas (savedata, variaveis da cave) cai em paginas ja
    // graváveis. So mexemos na protecao se a escrita direta falhar - trocar
    // protecao 40x por segundo em memoria do jogo e caro e desnecessario.
    if (WriteProcessMemory(handle, (LPVOID)addr, src, n, &put) && put == n)
        return true;

    DWORD old = 0;
    if (!VirtualProtectEx(handle, (LPVOID)addr, n, PAGE_EXECUTE_READWRITE, &old))
        return false;
    BOOL ok = WriteProcessMemory(handle, (LPVOID)addr, src, n, &put);
    DWORD tmp;
    VirtualProtectEx(handle, (LPVOID)addr, n, old, &tmp);
    return ok && put == n;
}

std::vector<HANDLE> Process::freezeThreads() const
{
    std::vector<HANDLE> out;
    if (!pid) return out;

    // Duas passadas: uma thread criada entre o snapshot e a escrita do patch
    // escaparia da suspensao. Repetimos ate um snapshot nao trazer nenhuma
    // thread nova (no maximo 4 rodadas, para nao travar aqui).
    for (int round = 0; round < 4; ++round) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) break;

        size_t before = out.size();
        THREADENTRY32 te; te.dwSize = sizeof(te);
        if (Thread32First(snap, &te)) {
            do {
                if (te.th32OwnerProcessID != pid) continue;

                bool known = false;
                for (size_t i = 0; i < out.size() && !known; ++i)
                    if (GetThreadId(out[i]) == te.th32ThreadID) known = true;
                if (known) continue;

                HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT,
                                      FALSE, te.th32ThreadID);
                if (!t) continue;
                if (SuspendThread(t) == (DWORD)-1) { CloseHandle(t); continue; }

                // SuspendThread so agenda a suspensao. Ler o contexto forca o
                // kernel a esperar a thread parar de verdade antes de voltar -
                // sem isso ainda daria para gravar o patch com a thread
                // executando exatamente aqueles bytes.
                CONTEXT ctx;
                memset(&ctx, 0, sizeof(ctx));
                ctx.ContextFlags = CONTEXT_CONTROL;
                GetThreadContext(t, &ctx);

                out.push_back(t);
            } while (Thread32Next(snap, &te));
        }
        CloseHandle(snap);
        if (out.size() == before) break;      // nenhuma thread nova
    }
    return out;
}

void Process::thawThreads(std::vector<HANDLE>& v) const
{
    for (size_t i = 0; i < v.size(); ++i) { ResumeThread(v[i]); CloseHandle(v[i]); }
    v.clear();
}

// -----------------------------------------------------------------------------
// Pattern
// -----------------------------------------------------------------------------
static int hexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool Pattern::parse(const char* text)
{
    tokens.clear(); caps.clear();
    const char* p = text;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;

        if (p[0] == '?') {                       // "?" ou "??"
            tokens.push_back(-1);
            while (*p == '?') ++p;
            continue;
        }
        if (p[0] == 's' || p[0] == 'S') {        // captura "sN"
            int n = 0; ++p;
            while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); ++p; }
            if (n <= 0 || n > 8) return false;
            caps.push_back(std::make_pair((int)tokens.size(), n));
            for (int i = 0; i < n; ++i) tokens.push_back(-1);
            continue;
        }
        int hi = hexVal(p[0]), lo = hexVal(p[1]);
        if (hi < 0 || lo < 0) return false;
        tokens.push_back(hi * 16 + lo);
        p += 2;
    }
    return !tokens.empty();
}

// -----------------------------------------------------------------------------
// Scanner
// -----------------------------------------------------------------------------
bool Scanner::loadImage()
{
    img.clear();
    if (!proc.handle || !proc.base) return false;

    // cabecalhos PE para localizar a secao de codigo
    uint8_t hdr[0x1000];
    if (!proc.read(proc.base, hdr, sizeof(hdr))) return false;
    if (*(uint16_t*)hdr != 0x5A4D) return false;

    uint32_t e_lfanew = *(uint32_t*)(hdr + 0x3C);
    if (e_lfanew + 0x108 > sizeof(hdr))          return false;
    if (*(uint32_t*)(hdr + e_lfanew) != 0x00004550) return false;

    uint16_t nsec   = *(uint16_t*)(hdr + e_lfanew + 6);
    uint16_t optsz  = *(uint16_t*)(hdr + e_lfanew + 20);
    uint32_t secOff = e_lfanew + 24 + optsz;

    uint32_t rva = 0, len = 0;
    for (int i = 0; i < nsec; ++i) {
        uint32_t o = secOff + i * 40;
        if (o + 40 > sizeof(hdr)) break;
        uint32_t chars = *(uint32_t*)(hdr + o + 36);
        if (chars & 0x20000000) {                       // IMAGE_SCN_MEM_EXECUTE
            rva = *(uint32_t*)(hdr + o + 12);
            len = *(uint32_t*)(hdr + o + 8);            // VirtualSize
            break;
        }
    }
    if (!rva || !len) return false;

    imgBase = proc.base + rva;
    img.assign(len, 0);

    // le em blocos: paginas isoladas podem falhar sem invalidar o resto
    const size_t CH = 0x100000;
    for (size_t off = 0; off < len; off += CH) {
        size_t n = (len - off < CH) ? (len - off) : CH;
        if (!proc.read(imgBase + off, &img[off], n)) {
            for (size_t k = 0; k < n; k += 0x1000) {
                size_t m = (n - k < 0x1000) ? (n - k) : 0x1000;
                proc.read(imgBase + off + k, &img[off + k], m);
            }
        }
    }
    return true;
}

std::vector<uint64_t> Scanner::find(const Pattern& pat,
                                    std::vector<uint64_t>* captures) const
{
    std::vector<uint64_t> hits;
    const size_t n = pat.tokens.size();
    if (img.size() < n || n == 0) return hits;

    // ancora = maior sequencia de bytes literais
    size_t ai = 0, an = 0, i = 0;
    while (i < n) {
        if (pat.tokens[i] < 0) { ++i; continue; }
        size_t j = i;
        while (j < n && pat.tokens[j] >= 0) ++j;
        if (j - i > an) { ai = i; an = j - i; }
        i = j;
    }
    if (an == 0) return hits;

    std::vector<uint8_t> needle(an);
    for (size_t k = 0; k < an; ++k) needle[k] = (uint8_t)pat.tokens[ai + k];

    const uint8_t* data = &img[0];
    const size_t   sz   = img.size();

    for (size_t pos = 0; pos + an <= sz; ++pos) {
        if (data[pos] != needle[0]) continue;
        if (memcmp(data + pos, &needle[0], an) != 0) continue;
        if (pos < ai) continue;
        size_t s = pos - ai;
        if (s + n > sz) continue;

        bool ok = true;
        for (size_t k = 0; k < n && ok; ++k)
            if (pat.tokens[k] >= 0 && data[s + k] != (uint8_t)pat.tokens[k]) ok = false;
        if (!ok) continue;

        if (hits.empty() && captures) {
            captures->clear();
            for (size_t c = 0; c < pat.caps.size(); ++c) {
                uint64_t v = 0;
                int co = pat.caps[c].first, cn = pat.caps[c].second;
                memcpy(&v, data + s + co, cn);
                captures->push_back(v);
            }
        }
        hits.push_back(imgBase + s);
        if (hits.size() > 8) break;      // padroes devem ser unicos
    }
    return hits;
}

// -----------------------------------------------------------------------------
// Asm
// -----------------------------------------------------------------------------
Asm& Asm::db(std::initializer_list<int> bytes)
{
    for (int b : bytes) buf.push_back((uint8_t)(b & 0xFF));
    return *this;
}

Asm& Asm::raw(const uint8_t* p, size_t n)
{
    for (size_t i = 0; i < n; ++i) buf.push_back(p[i]);
    return *this;
}

Asm& Asm::label(const char* name)
{
    labels[name] = buf.size();
    return *this;
}

Asm& Asm::rip(std::initializer_list<int> prefix, uint64_t target,
              std::initializer_list<int> suffix)
{
    for (int b : prefix) buf.push_back((uint8_t)(b & 0xFF));
    size_t at = buf.size();
    buf.insert(buf.end(), 4, 0);
    for (int b : suffix) buf.push_back((uint8_t)(b & 0xFF));

    int64_t rel = (int64_t)target - (int64_t)(base + buf.size());
    int32_t r32 = (int32_t)rel;
    memcpy(&buf[at], &r32, 4);
    return *this;
}

Asm& Asm::jmp(const char* lbl)
{
    buf.push_back(0xE9);
    fixups.push_back(std::make_pair(buf.size(), std::string(lbl)));
    buf.insert(buf.end(), 4, 0);
    return *this;
}

Asm& Asm::jmpAbs(uint64_t target)
{
    buf.push_back(0xE9);
    size_t at = buf.size();
    buf.insert(buf.end(), 4, 0);
    int32_t rel = (int32_t)((int64_t)target - (int64_t)(base + buf.size()));
    memcpy(&buf[at], &rel, 4);
    return *this;
}

Asm& Asm::jcc(int cc, const char* lbl)
{
    buf.push_back(0x0F);
    buf.push_back((uint8_t)cc);
    fixups.push_back(std::make_pair(buf.size(), std::string(lbl)));
    buf.insert(buf.end(), 4, 0);
    return *this;
}

Asm& Asm::cmpVar32(uint64_t varAddr, int imm8)
{
    return rip({0x83, 0x3D}, varAddr, {imm8 & 0xFF});
}

std::vector<uint8_t> Asm::finish()
{
    for (size_t i = 0; i < fixups.size(); ++i) {
        size_t at = fixups[i].first;
        std::map<std::string, size_t>::iterator it = labels.find(fixups[i].second);
        if (it == labels.end()) continue;
        int32_t rel = (int32_t)((int64_t)it->second - (int64_t)(at + 4));
        memcpy(&buf[at], &rel, 4);
    }
    return buf;
}
