#if __has_include("editor/pcm_bank_service.h")
#include "editor/pcm_bank_service.h"
#include "editor/mucom_compile_service.h"
#include "tests/test_support.h"
#include "tests/phase6/pcm_test_fixtures.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
namespace fs = std::filesystem;
using namespace mucom88;
using namespace mucom88_test;
namespace {
struct Workspace {
    fs::path root = fs::temp_directory_path() / ("mucom88-pcm-contract-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Workspace() { fs::create_directories(root / u8"日本語 path"); }
    ~Workspace() { std::error_code ignored; fs::remove_all(root, ignored); }
};
void Write(const fs::path &path, const PcmBytes &b) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out.write(reinterpret_cast<const char *>(b.data()), b.size());
}
void Text(const fs::path &path, const std::string &s) { Write(path, PcmBytes(s.begin(), s.end())); }
PcmBytes Read(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
std::vector<std::string> Inventory(const fs::path &root) {
    std::vector<std::string> paths;
    for (const auto &entry : fs::recursive_directory_iterator(root)) paths.push_back(entry.path().string());
    std::sort(paths.begin(), paths.end()); return paths;
}
void Range(TestContext &test, const PcmBytes &b, int slot, int start, int length) {
    CHECK(test, b.size() >= 0x400);
    if (b.size() < 0x400) return;
    CHECK(test, PcmReadWord(b, slot * 32 + 28) == start);
    CHECK(test, PcmReadWord(b, slot * 32 + 30) == length);
}
void Failure(TestContext &test, const ServiceError &error, ServiceErrorCode code, const char *file) {
    CHECK(test, error.code == code);
    CHECK(test, !error.message.empty());
    CHECK(test, error.path.find(file) != std::string::npos);
}
void RawAndLayout(TestContext &test, const fs::path &dir) {
    PcmBankService pcm;
    const PcmBytes first{1,2,3,4,5}, second{9,8,7,6};
    Write(dir / "first.adpcm", first); Write(dir / "second.bin", second);
    Text(dir / "layout.txt", "first.adpcm\nsecond.bin\n");
    const auto inventory = Inventory(dir);
    const auto bank = pcm.BuildFromList((dir / "layout.txt").string());
    CHECK(test, bank.Succeeded()); if (!bank.Succeeded()) return;
    CHECK(test, PcmBankStructure(bank.value.bytes));
    CHECK(test, bank.value.bytes.size() == 0x40c);
    Range(test, bank.value.bytes, 0, 0, 2); Range(test, bank.value.bytes, 1, 2, 1);
    if (bank.value.bytes.size() == 0x40c) {
        CHECK(test, PcmBytes(bank.value.bytes.begin() + 0x400, bank.value.bytes.end()) ==
            PcmBytes({1,2,3,4,5,0,0,0,9,8,7,6}));
        CHECK(test, std::string(bank.value.bytes.begin(), bank.value.bytes.begin()+16) == "first           ");
        CHECK(test, std::all_of(bank.value.bytes.begin()+64, bank.value.bytes.begin()+0x400,
            [](std::uint8_t c) { return c == 0; }));
    }
    CHECK(test, Inventory(dir) == inventory);
    CHECK(test, Read(dir / "first.adpcm") == first);
    const auto retained = bank.value.bytes;
    Text(dir / "again.txt", "second.bin\n");
    const auto again = pcm.BuildFromList((dir / "again.txt").string());
    CHECK(test, again.Succeeded()); CHECK(test, bank.value.bytes == retained);
    Write(dir / "abcdefghijklmnopqrst.adpcm", {1,2,3,4});
    Text(dir / "long-name.txt", "abcdefghijklmnopqrst.adpcm\n");
    const auto longName=pcm.BuildFromList((dir / "long-name.txt").string());
    CHECK(test,longName.Succeeded());
    if(longName.Succeeded() && longName.value.bytes.size()>=0x400) CHECK(test,
        std::string(longName.value.bytes.begin(),longName.value.bytes.begin()+16)=="abcdefghijklmnop");
    // Every sub-word length rounds up rather than truncating its length field.
    for (int length : {1,2,3,4,7,8,9}) {
        Write(dir / "short.adpcm", PcmBytes(length, 6)); Text(dir / "short.txt", "short.adpcm\n");
        const auto shortBank = pcm.BuildFromList((dir / "short.txt").string());
        CHECK(test, shortBank.Succeeded()); if (!shortBank.Succeeded()) continue;
        CHECK(test, shortBank.value.bytes.size() == 0x400 + std::size_t((length+3)/4*4));
        Range(test, shortBank.value.bytes, 0, 0, (length+3)/4);
    }
}
void ListGrammar(TestContext &test, const fs::path &dir) {
    PcmBankService pcm;
    Write(dir / "with spaces.adpcm", {1,2,3,4});
    const std::string absolute = (dir / "with spaces.adpcm").string();
    for (const std::string &contents : {std::string("with spaces.adpcm\n"),
        std::string("\xef\xbb\xbf\n \t\r\nwith spaces.adpcm\r\n"),
        std::string("with spaces.adpcm\r"), absolute}) {
        Text(dir / "grammar.txt", contents);
        const auto bank = pcm.BuildFromList((dir / "grammar.txt").string());
        CHECK(test, bank.Succeeded());
        if (bank.Succeeded()) { CHECK(test, bank.value.bytes.size() == 0x404); Range(test, bank.value.bytes, 0, 0, 1); }
    }
    // Duplicate paths describe separate slots; no deduplication or sorting.
    Text(dir / "duplicates.txt", "with spaces.adpcm\nwith spaces.adpcm\n");
    const auto duplicate = pcm.BuildFromList((dir / "duplicates.txt").string());
    CHECK(test, duplicate.Succeeded());
    if (duplicate.Succeeded()) { Range(test, duplicate.value.bytes, 0, 0, 1); Range(test, duplicate.value.bytes, 1, 1, 1); }
    for (const auto &contents : {std::string(), std::string("\n \t\n"),
        std::string("with spaces.adpcm\0evil", 22), std::string(1, char(0xff))}) {
        Text(dir / "invalid-list.txt", contents);
        const auto bad = pcm.BuildFromList((dir / "invalid-list.txt").string());
        CHECK(test, !bad.Succeeded()); Failure(test, bad.error, ServiceErrorCode::InvalidData, "invalid-list.txt");
        CHECK(test, bad.value.bytes.empty());
    }
    const auto missing = pcm.BuildFromList((dir / "not-found.txt").string());
    Failure(test, missing.error, ServiceErrorCode::NotFound, "not-found.txt");
    Text(dir / "missing-sample.txt", "with spaces.adpcm\nmissing.wav\n");
    const auto absent = pcm.BuildFromList((dir / "missing-sample.txt").string());
    Failure(test, absent.error, ServiceErrorCode::NotFound, "missing.wav");
    CHECK(test, absent.value.bytes.empty());
    CHECK(test, absent.error.message.find("line 2") != std::string::npos);
    Write(dir / "empty.adpcm", {}); Text(dir / "empty.txt", "empty.adpcm\n");
    Failure(test, pcm.BuildFromList((dir / "empty.txt").string()).error, ServiceErrorCode::InvalidData, "empty.adpcm");
    Write(dir / "unknown.mp3", {1,2,3,4}); Text(dir / "unknown.txt", "unknown.mp3\n");
    Failure(test, pcm.BuildFromList((dir / "unknown.txt").string()).error, ServiceErrorCode::UnsupportedFormat, "unknown.mp3");
}
void Limits(TestContext &test, const fs::path &dir) {
    PcmBankService pcm; Write(dir / "unit.adpcm", {1,2,3,4});
    std::string list;
    for (int i=0; i<32; ++i) list += "unit.adpcm\n";
    Text(dir / "32.txt", list);
    const auto full = pcm.BuildFromList((dir / "32.txt").string());
    CHECK(test, full.Succeeded());
    if (full.Succeeded()) { CHECK(test, PcmBankStructure(full.value.bytes)); Range(test, full.value.bytes, 31, 31, 1); }
    Text(dir / "33.txt", list + "unit.adpcm\n");
    const auto tooMany = pcm.BuildFromList((dir / "33.txt").string());
    Failure(test, tooMany.error, ServiceErrorCode::InvalidData, "33.txt"); CHECK(test, tooMany.value.bytes.empty());
    for (int size : {0x3fffc, 0x40000, 0x40004}) {
        Write(dir / "large.adpcm", PcmBytes(size, 7)); Text(dir / "large.txt", "large.adpcm\n");
        const auto bank = pcm.BuildFromList((dir / "large.txt").string());
        CHECK(test, bank.Succeeded() == (size == 0x3fffc));
        if (bank.Succeeded()) { CHECK(test, PcmBankStructure(bank.value.bytes)); Range(test, bank.value.bytes, 0, 0, 65535); }
        else CHECK(test, bank.error.code == ServiceErrorCode::InvalidData);
    }
    Write(dir / "large.adpcm", PcmBytes(0x3fffc, 7));
    Text(dir / "aggregate.txt", "large.adpcm\nunit.adpcm\n");
    const auto aggregate = pcm.BuildFromList((dir / "aggregate.txt").string());
    CHECK(test, aggregate.error.code == ServiceErrorCode::InvalidData); CHECK(test, aggregate.value.bytes.empty());
}
void DataDirectory(TestContext &test, const fs::path &dir) {
    fs::create_directories(dir); PcmBankService pcm;
    PcmBytes header(0x400, 0);
    // Sparse source slots stay sparse: physical VOICE._3 must not become slot 2.
    for (int slot : {0,2}) {
        header[slot*32] = 'A' + slot; PcmWord(header, slot*32+16, 12);
        PcmWord(header, slot*32+18, 14); PcmWord(header, slot*32+26, 3);
        Write(dir / ("VOICE._" + std::to_string(slot+1)), {1,2,3,4,5,6,7,8});
    }
    Write(dir / "DATA", header); const auto originalFiles = Inventory(dir);
    const auto bank = pcm.BuildFromDataDirectory(dir.string());
    CHECK(test, bank.Succeeded());
    if (bank.Succeeded()) {
        CHECK(test, PcmBankStructure(bank.value.bytes)); CHECK(test, bank.value.bytes.size() == 0x410);
        Range(test, bank.value.bytes, 0, 0, 2); Range(test, bank.value.bytes, 2, 2, 2);
        Range(test, bank.value.bytes, 1, 0, 0);
        if (bank.value.bytes.size() >= 0x400) {
            CHECK(test, bank.value.bytes[0] == 'A'); CHECK(test, bank.value.bytes[64] == 'C');
            CHECK(test, PcmReadWord(bank.value.bytes, 26) == 3);
        }
    }
    CHECK(test, Read(dir / "DATA") == header); CHECK(test, Inventory(dir) == originalFiles);
    const auto allDir = dir / "all32"; fs::create_directories(allDir);
    PcmBytes allHeader(0x400,0);
    for (int slot=0;slot<32;++slot) {
        allHeader[slot*32]='A'; PcmWord(allHeader,slot*32+18,1);
        Write(allDir / ("VOICE._"+std::to_string(slot+1)), {1,2,3,4});
    }
    Write(allDir / "DATA",allHeader);
    const auto all=pcm.BuildFromDataDirectory(allDir.string()); CHECK(test, all.Succeeded());
    if(all.Succeeded()) { CHECK(test,PcmBankStructure(all.value.bytes)); Range(test,all.value.bytes,31,31,1); }

    fs::remove(dir / "VOICE._3");
    const auto missing = pcm.BuildFromDataDirectory(dir.string());
    Failure(test, missing.error, ServiceErrorCode::NotFound, "VOICE._3"); CHECK(test, missing.value.bytes.empty());
    Write(dir / "VOICE._3", {1,2,3,4});
    Failure(test, pcm.BuildFromDataDirectory(dir.string()).error, ServiceErrorCode::InvalidData, "VOICE._3");
    for (const auto size : {std::size_t(0), std::size_t(0x3ff), std::size_t(0x401)}) {
        Write(dir / "DATA", PcmBytes(size, 0));
        Failure(test, pcm.BuildFromDataDirectory(dir.string()).error, ServiceErrorCode::InvalidData, "DATA");
    }
    auto reversed = header; PcmWord(reversed, 18, 11); Write(dir / "DATA", reversed);
    Failure(test, pcm.BuildFromDataDirectory(dir.string()).error, ServiceErrorCode::InvalidData, "DATA");
    Write(dir / "DATA", PcmBytes(0x400, 0));
    Failure(test, pcm.BuildFromDataDirectory(dir.string()).error, ServiceErrorCode::InvalidData, "DATA");
    fs::remove(dir / "DATA");
    Failure(test, pcm.BuildFromDataDirectory(dir.string()).error, ServiceErrorCode::NotFound, "DATA");
}
void WavFormats(TestContext &test, const fs::path &dir) {
    PcmBankService pcm;
    Text(dir / "wave.txt", "wave.WAV\n");
    for (int channels : {1,2}) for (bool junk : {false,true}) {
        Write(dir / "wave.WAV", PcmSilenceWav(channels,16,16000,junk));
        const auto before = Inventory(dir);
        const auto bank = pcm.BuildFromList((dir / "wave.txt").string());
        CHECK(test, bank.Succeeded());
        if (bank.Succeeded()) {
            CHECK(test, bank.value.bytes.size() == 0x420); Range(test, bank.value.bytes, 0, 0, 8);
            if (bank.value.bytes.size() == 0x420) CHECK(test, PcmBytes(bank.value.bytes.begin()+0x400,
                bank.value.bytes.end()) == PcmBytes(32, 0x08));
        }
        CHECK(test, Inventory(dir) == before);
    }
    for (int kind=0; kind<12; ++kind) {
        auto wav = PcmSilenceWav();
        auto code = ServiceErrorCode::InvalidData;
        switch (kind) {
        case 0: wav.resize(4); break;
        case 1: wav[0]='X'; break;
        case 2: PcmDword(wav,4,0xffffffff); break;
        case 3: PcmDword(wav,40,0xffffffff); break;
        case 4: PcmWord(wav,20,3); code=ServiceErrorCode::UnsupportedFormat; break;
        case 5: PcmWord(wav,34,8); code=ServiceErrorCode::UnsupportedFormat; break;
        case 6: PcmWord(wav,22,3); code=ServiceErrorCode::UnsupportedFormat; break;
        case 7: PcmDword(wav,24,0); break;
        case 8: PcmWord(wav,32,1); break;
        case 9: PcmDword(wav,28,1); break;
        case 10: PcmDword(wav,40,127); wav.pop_back(); PcmDword(wav,4,wav.size()-8); break;
        case 11: PcmMagic(wav,36,"JUNK"); break; // missing data
        }
        Write(dir / "wave.WAV", wav); const auto before = Inventory(dir);
        const auto invalid = pcm.BuildFromList((dir / "wave.txt").string());
        Failure(test, invalid.error, code, "wave.WAV"); CHECK(test, invalid.value.bytes.empty());
        CHECK(test, Inventory(dir) == before);
    }
}
void ResourceBounds(TestContext &test,const fs::path &dir) {
    PcmBankService pcm;
    CHECK(test,pcm.BuildFromList("").error.code==ServiceErrorCode::InvalidArgument);
    CHECK(test,pcm.BuildFromDataDirectory("").error.code==ServiceErrorCode::InvalidArgument);
    const std::string nul("bad\0path",8);
    CHECK(test,pcm.BuildFromList(nul).error.code==ServiceErrorCode::InvalidArgument);
    CHECK(test,pcm.BuildFromDataDirectory(nul).error.code==ServiceErrorCode::InvalidArgument);
    // A tiny WAV can still demand excessive output after resampling. Reject
    // it before allocating a decoded buffer, including the padding estimate.
    auto wav=PcmSilenceWav(1,16,1);
    Write(dir / "slow.wav",wav); Text(dir / "slow.txt","slow.wav\n");
    const auto before=Inventory(dir);
    const auto oversized=pcm.BuildFromList((dir / "slow.txt").string());
    Failure(test,oversized.error,ServiceErrorCode::InvalidData,"slow.wav");
    CHECK(test,oversized.value.bytes.empty()); CHECK(test,Inventory(dir)==before);
    for(int rate : {8000,44100}) {
        Write(dir / "slow.wav",PcmSilenceWav(1,16,rate));
        const auto scaled=pcm.BuildFromList((dir / "slow.txt").string());
        CHECK(test,scaled.Succeeded());
        if(scaled.Succeeded()) {
            const auto expected=rate==8000 ? 64u : 32u;
            CHECK(test,scaled.value.bytes.size()==0x400+expected);
            if(scaled.value.bytes.size()==0x400+expected) CHECK(test,
                PcmBytes(scaled.value.bytes.begin()+0x400,scaled.value.bytes.end())==PcmBytes(expected,8));
        }
    }
}

void SaveAndCompile(TestContext &test, const fs::path &dir) {
    PcmBankService pcm; Write(dir / "save.adpcm", {1,2,3,4,5,6,7,8});
    Text(dir / "save.txt", "save.adpcm\n");
    const auto bank = pcm.BuildFromList((dir / "save.txt").string());
    CHECK(test, bank.Succeeded()); if (!bank.Succeeded()) return;
    const auto path = dir / u8"保存 bank.bin"; Write(path, {99,98,97});
    const auto before = bank.value.bytes;
    for (const auto &input : {dir / "save.adpcm",dir / "save.txt"}) {
        const auto original=Read(input);
        CHECK(test,!pcm.Save(bank.value,input.string()).Succeeded()); CHECK(test,Read(input)==original);
    }
    fs::create_symlink(dir / "save.adpcm",dir / "source-symlink.bin");
    fs::create_hard_link(dir / "save.adpcm",dir / "source-hardlink.bin");
    for(const auto &alias : {dir / "source-symlink.bin",dir / "source-hardlink.bin"}) {
        const auto original=Read(alias);
        CHECK(test,!pcm.Save(bank.value,alias.string()).Succeeded()); CHECK(test,Read(alias)==original);
    }

    CHECK(test, pcm.Save(bank.value,path.string()).Succeeded()); CHECK(test, Read(path) == before);
    auto invalid = bank.value; invalid.bytes.resize(20);
    const auto bad = pcm.Save(invalid,path.string());
    CHECK(test, bad.error.code == ServiceErrorCode::InvalidData); CHECK(test, Read(path) == before);
    fs::create_directories(dir / "directory.bin"); Text(dir / "directory.bin/sentinel", "retain");
    CHECK(test, !pcm.Save(bank.value,(dir / "directory.bin").string()).Succeeded());
    CHECK(test, Read(dir / "directory.bin/sentinel") == PcmBytes({'r','e','t','a','i','n'}));
    CHECK(test, !pcm.Save(bank.value,(dir / "absent/output.bin").string()).Succeeded());
    CHECK(test, !fs::exists(dir / "absent")); CHECK(test, bank.value.bytes == before);
    const auto inventory = Inventory(dir);
    auto badRange = bank.value; PcmWord(badRange.bytes,30,65535);
    CHECK(test, pcm.Save(badRange,path.string()).error.code == ServiceErrorCode::InvalidData);
    CHECK(test, Read(path) == before); CHECK(test, Inventory(dir) == inventory);
    // This integration runs without the unrelated export-validator header.
    MucomCompileService compiler; CompileRequest request;
    request.source_path=(dir / "song.muc").string(); request.resource_directory=dir.string();
    request.utf8_text="#mucom88 1.5\n#pcm 保存 bank.bin\nA C96 t190 @3 o4 v10 l16 c\nK C96 @0 v30 l16 c\n";
    const auto result = compiler.Compile(request); CHECK(test, result.Succeeded());
    if (result.Succeeded()) {
        CHECK(test, result.song->has_embedded_pcm);
        // MUB header stores byte offsets/lengths at 20 and 24. Read independently.
        const auto &mub=result.song->mub_bytes;
        if (mub.size() >= 28) {
            auto dword=[&](int at) { std::uint32_t n=0; for(int i=0;i<4;++i) n|=std::uint32_t(mub[at+i])<<(i*8); return n; };
            const auto at=dword(20), length=dword(24);
            CHECK(test, length == before.size()); CHECK(test, at <= mub.size() && length <= mub.size()-at);
            if (at <= mub.size() && length <= mub.size()-at) CHECK(test,
                PcmBytes(mub.begin()+at,mub.begin()+at+length) == before);
        } else CHECK(test, false);
    }
}
}
int main() {
    TestContext test; Workspace workspace;
    const auto initial = fs::current_path(); const auto dir=workspace.root / u8"日本語 path";
    std::cout << "PCM-01..04 raw layout/padding/ownership\n"; RawAndLayout(test,dir);
    std::cout << "PCM-05..08 list grammar/files/errors\n"; ListGrammar(test,dir);
    std::cout << "PCM-09..10 entry/capacity limits\n"; Limits(test,dir);
    std::cout << "PCM-11..14 DATA/sparse/errors\n"; DataDirectory(test,workspace.root / "data");
    std::cout << "PCM-15..17 WAV/conversion/errors\n"; WavFormats(test,dir);
    std::cout << "PCM-18..21 save/protection/compile\n"; SaveAndCompile(test,dir);
    std::cout << "PCM-22 resource bounds/resampling/input paths\n"; ResourceBounds(test,dir);
    CHECK(test, fs::current_path() == initial);
    return test.ExitCode();
}
#else
#include <iostream>
int main() { std::cout << "SKIP: editor/pcm_bank_service.h is not implemented yet\n"; return 77; }
#endif
