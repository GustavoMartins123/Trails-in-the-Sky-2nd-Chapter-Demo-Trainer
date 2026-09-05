// =============================================================================
//  selftest.cpp - valida assinaturas e trampolins sem precisar do jogo aberto.
//
//    SoraSelfTest.exe "..\sora_2nd.exe"
//
//  1) carrega a secao de codigo direto do arquivo .exe
//  2) roda cada assinatura do trainer e mostra endereco + capturas
//  3) monta os dois trampolins com o mesmo Asm usado em producao e
//     despeja os bytes para conferencia no disassembler
// =============================================================================
#include "trainer.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

namespace sig {
extern const char* BATTLE_MGR;
extern const char* SAVE_MGR;
extern const char* STATUS_ARR;
extern const char* MIRA;
extern const char* SEPITH;
extern const char* ADDHP;
extern const char* EXPCALC;
}
namespace sig {
const char* BATTLE_MGR = "48 8B 05 s4 48 8B 98 48 24 00 00 8B 80 50 24 00 00 48 8D 3C C3";
const char* SAVE_MGR   = "48 8B 05 s4 0F B7 94 88 s4 48 8B CF";
const char* STATUS_ARR = "48 69 C2 s4 48 05 s4 49 03 C2";
const char* MIRA       = "89 82 s4 45 33 C9";
const char* SEPITH     = "89 82 s4 44 8B 93 AC 5B 12 00";
const char* ADDHP      = "48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 30 48 8B B9 E0 05 00 00";
const char* EXPCALC    = "8B C5 48 8B AC 24 88 00 00 00 48 81 C4 90 00 00 00";
}

static std::vector<uint8_t> g_text;
static uint64_t g_textVA = 0;
static uint64_t g_imageBase = 0;

static bool loadTextSection(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) { printf("nao abriu %s\n", path); return false; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> d(n);
    if (fread(&d[0], 1, n, f) != (size_t)n) { fclose(f); return false; }
    fclose(f);

    uint32_t e = *(uint32_t*)&d[0x3C];
    g_imageBase = *(uint64_t*)&d[e + 24 + 24];
    uint16_t nsec  = *(uint16_t*)&d[e + 6];
    uint16_t optsz = *(uint16_t*)&d[e + 20];
    uint32_t so    = e + 24 + optsz;
    for (int i = 0; i < nsec; ++i) {
        uint32_t o = so + i * 40;
        uint32_t chars = *(uint32_t*)&d[o + 36];
        if (chars & 0x20000000) {
            uint32_t vs = *(uint32_t*)&d[o + 8];
            uint32_t va = *(uint32_t*)&d[o + 12];
            uint32_t rs = *(uint32_t*)&d[o + 16];
            uint32_t ro = *(uint32_t*)&d[o + 20];
            uint32_t len = vs < rs ? vs : rs;
            g_text.assign(d.begin() + ro, d.begin() + ro + len);
            g_textVA = g_imageBase + va;
            printf("secao de codigo: VA=%016llX  %u bytes\n",
                   (unsigned long long)g_textVA, len);
            return true;
        }
    }
    return false;
}

static std::vector<uint64_t> scan(const Pattern& pat, std::vector<uint64_t>* caps)
{
    std::vector<uint64_t> hits;
    size_t n = pat.tokens.size();
    size_t ai = 0, an = 0, i = 0;
    while (i < n) {
        if (pat.tokens[i] < 0) { ++i; continue; }
        size_t j = i; while (j < n && pat.tokens[j] >= 0) ++j;
        if (j - i > an) { ai = i; an = j - i; }
        i = j;
    }
    const uint8_t* data = &g_text[0];
    size_t sz = g_text.size();
    for (size_t pos = 0; pos + an <= sz; ++pos) {
        if (memcmp(data + pos, &pat.tokens[ai], 0) != 0) {}
        bool m = true;
        for (size_t k = 0; k < an; ++k)
            if (data[pos + k] != (uint8_t)pat.tokens[ai + k]) { m = false; break; }
        if (!m || pos < ai) continue;
        size_t s = pos - ai;
        if (s + n > sz) continue;
        bool ok = true;
        for (size_t k = 0; k < n && ok; ++k)
            if (pat.tokens[k] >= 0 && data[s + k] != (uint8_t)pat.tokens[k]) ok = false;
        if (!ok) continue;
        if (hits.empty() && caps) {
            caps->clear();
            for (size_t c = 0; c < pat.caps.size(); ++c) {
                uint64_t v = 0;
                memcpy(&v, data + s + pat.caps[c].first, pat.caps[c].second);
                caps->push_back(v);
            }
        }
        hits.push_back(g_textVA + s);
    }
    return hits;
}

static void test(const char* name, const char* patt)
{
    Pattern p;
    if (!p.parse(patt)) { printf("%-14s PADRAO INVALIDO\n", name); return; }
    std::vector<uint64_t> caps;
    std::vector<uint64_t> hits = scan(p, &caps);
    printf("%-14s hits=%zu", name, hits.size());
    if (hits.size() == 1) {
        printf("  addr=%016llX", (unsigned long long)hits[0]);
        for (size_t i = 0; i < caps.size(); ++i)
            printf("  cap%zu=0x%llX", i, (unsigned long long)caps[i]);
        int64_t rip = (int64_t)hits[0] + 7 + (int32_t)(uint32_t)(caps.empty() ? 0 : caps[0]);
        if (!caps.empty()) printf("  [rip+7 -> %016llX]", (unsigned long long)rip);
    }
    printf("\n");
}

static void dumpBytes(const char* tag, uint64_t base, const std::vector<uint8_t>& b)
{
    printf("--- %s  base=%016llX  %zu bytes\n", tag, (unsigned long long)base, b.size());
    for (size_t i = 0; i < b.size(); ++i) {
        printf("%02X", b[i]);
        printf((i % 32 == 31) ? "\n" : " ");
    }
    printf("\n");
}

int main(int argc, char** argv)
{
    const char* path = (argc > 1) ? argv[1] : "..\\sora_2nd.exe";
    if (!loadTextSection(path)) return 1;

    printf("\n== assinaturas ==\n");
    test("battle_mgr",  sig::BATTLE_MGR);
    test("save_mgr",    sig::SAVE_MGR);
    test("status_arr",  sig::STATUS_ARR);
    test("mira",        sig::MIRA);
    test("sepith",      sig::SEPITH);
    test("addhp",       sig::ADDHP);
    test("expcalc",     sig::EXPCALC);

    // --- trampolins montados com enderecos ficticios mas realistas -----------
    const uint64_t CAVE = 0x13F000000ULL;
    const uint64_t vPartyLo = CAVE + 0x00, vPartyHi = CAVE + 0x08;
    const uint64_t vGod = CAVE + 0x10, vOneHit = CAVE + 0x14;
    const uint64_t vDefMul = CAVE + 0x18, vDmgMul = CAVE + 0x1C, vExpMul = CAVE + 0x20;
    const uint64_t T0 = CAVE + 0x1000, T1 = CAVE + 0x1400;
    const uint64_t SITE0 = 0x1400E4900ULL, SITE1 = 0x1400F103EULL;

    uint8_t orig0[5]  = { 0x48, 0x89, 0x5C, 0x24, 0x18 };
    uint8_t orig1[10] = { 0x8B, 0xC5, 0x48, 0x8B, 0xAC, 0x24, 0x88, 0x00, 0x00, 0x00 };

    printf("\n== trampolins ==\n");
    {
        Asm a(T0);
        a.db({0x85, 0xD2});
        a.jcc(Asm::NS, "orig");
        a.db({0x50});
        a.db({0x41, 0x52});
        a.rip({0x4C, 0x8B, 0x15}, vPartyLo);
        a.db({0x4D, 0x85, 0xD2});
        a.jcc(Asm::E, "done");
        a.db({0x48, 0x8B, 0x01});
        a.db({0x49, 0x3B, 0xC2});
        a.jcc(Asm::B, "enemy");
        a.rip({0x4C, 0x8B, 0x15}, vPartyHi);
        a.db({0x49, 0x3B, 0xC2});
        a.jcc(Asm::AE, "enemy");
        a.cmpVar32(vGod, 1);
        a.jcc(Asm::NE, "pdef");
        a.db({0x31, 0xD2});
        a.jmp("done");
        a.label("pdef");
        a.cmpVar32(vDefMul, 0);
        a.jcc(Asm::LE, "done");
        a.db({0xF3, 0x0F, 0x2A, 0xEA});
        a.rip({0xF3, 0x0F, 0x5E, 0x2D}, vDefMul);
        a.db({0xF3, 0x0F, 0x2C, 0xD5});
        a.jmp("done");
        a.label("enemy");
        a.cmpVar32(vOneHit, 1);
        a.jcc(Asm::NE, "emul");
        a.db({0xBA, 0xC1, 0xBD, 0xF0, 0xFF});
        a.jmp("done");
        a.label("emul");
        a.cmpVar32(vDmgMul, 0);
        a.jcc(Asm::LE, "done");
        a.db({0xF3, 0x0F, 0x2A, 0xEA});
        a.rip({0xF3, 0x0F, 0x59, 0x2D}, vDmgMul);
        a.db({0xF3, 0x0F, 0x2C, 0xD5});
        a.label("done");
        a.db({0x41, 0x5A});
        a.db({0x58});
        a.label("orig");
        a.raw(orig0, sizeof(orig0));
        a.jmpAbs(SITE0 + 5);
        dumpBytes("AddHp", T0, a.finish());
    }
    {
        Asm a(T1);
        a.db({0x8B, 0xC5});
        a.cmpVar32(vExpMul, 0);
        a.jcc(Asm::LE, "done");
        a.db({0xF3, 0x0F, 0x2A, 0xE8});
        a.rip({0xF3, 0x0F, 0x59, 0x2D}, vExpMul);
        a.db({0xF3, 0x0F, 0x2C, 0xC5});
        a.db({0x85, 0xC0});
        a.jcc(Asm::NS, "done");
        a.db({0xB8, 0xFF, 0xC9, 0x9A, 0x3B});
        a.label("done");
        a.raw(orig1 + 2, sizeof(orig1) - 2);
        a.jmpAbs(SITE1 + 10);
        dumpBytes("ExpCalc", T1, a.finish());
    }
    return 0;
}
