#include "editor/pcm_bank_service.h"
#include "editor/document_service.h"
#include "adpcm.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <unistd.h>
namespace mucom88 {
namespace {
namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t kHeader = 1024, kBodyLimit = 0x40000;
ServiceError Error(ServiceErrorCode code, const std::string &message, const std::string &path) {
    return {code, message, path, true};
}
std::uint16_t Word(const Bytes &b, std::size_t p) { return b[p] | (std::uint16_t(b[p+1]) << 8); }
std::uint32_t Dword(const Bytes &b, std::size_t p) {
    return std::uint32_t(Word(b,p)) | (std::uint32_t(Word(b,p+2)) << 16);
}
void PutWord(Bytes &b, std::size_t p, std::size_t n) { b[p]=n&255; b[p+1]=(n>>8)&255; }
std::string Absolute(const std::string &path) {
    std::error_code ec;
    const auto absolute=fs::absolute(path,ec);
    return ec ? path : absolute.lexically_normal().string();
}
ServiceResult<Bytes> Read(const std::string &path, std::size_t limit) {
    if(path.empty() || path.find('\0')!=std::string::npos)
        return {{},Error(ServiceErrorCode::InvalidArgument,"Choose an input file path.",path)};
    std::error_code ec;
    if (!fs::exists(path,ec)) return {{},Error(ec ? ServiceErrorCode::IoError : ServiceErrorCode::NotFound,
        "Input file was not found: " + path,path)};
    if (!fs::is_regular_file(path,ec)) return {{},Error(ServiceErrorCode::IoError,"Input is not a regular file.",path)};
    const auto size=fs::file_size(path,ec);
    if (ec) return {{},Error(ServiceErrorCode::IoError,"Cannot read input file size.",path)};
    if (size>limit) return {{},Error(ServiceErrorCode::InvalidData,"Input exceeds the supported size limit.",path)};
    Bytes bytes(static_cast<std::size_t>(size));
    std::ifstream input(path,std::ios::binary);
    if (!input || (size && !input.read(reinterpret_cast<char *>(bytes.data()),bytes.size())))
        return {{},Error(ServiceErrorCode::IoError,"Cannot read input file.",path)};
    // Catch growth between stat and read rather than silently truncating input.
    if (input.peek()!=std::char_traits<char>::eof())
        return {{},Error(ServiceErrorCode::Conflict,"Input changed while it was being read.",path)};
    return {std::move(bytes),{}};
}
bool Magic(const Bytes &b,std::size_t at,const char *s) {
    return std::equal(s,s+4,b.begin()+at);
}
ServiceResult<Bytes> Wav(const Bytes &b,const std::string &path) {
    auto bad=[&](const char *s) -> ServiceResult<Bytes> { return {{},Error(ServiceErrorCode::InvalidData,s,path)}; };
    if(b.size()<12 || !Magic(b,0,"RIFF") || !Magic(b,8,"WAVE")) return bad("Expected a RIFF/WAVE file.");
    const std::uint64_t end=std::uint64_t(Dword(b,4))+8;
    if(end!=b.size()) return bad("WAV RIFF length does not match the file.");
    std::size_t format=0, data=0, dataSize=0;
    for(std::size_t at=12;at<end;) {
        if(end-at<8) return bad("Truncated WAV chunk header.");
        const std::uint64_t size=Dword(b,at+4), payload=at+8;
        const auto next=payload+size+(size&1);
        if(next>end) return bad("WAV chunk extends beyond RIFF.");
        if(Magic(b,at,"fmt ")) {
            if(format || size<16) return bad("Missing or duplicate WAV format parameters.");
            format=at+8;
        } else if(Magic(b,at,"data")) {
            if(data) return bad("Duplicate WAV data chunk.");
            data=at+8; dataSize=static_cast<std::size_t>(size);
        }
        at=static_cast<std::size_t>(next);
    }
    if(!format || !data || !dataSize) return bad("WAV requires format and nonempty data chunks.");
    const auto channels=Word(b,format+2), bits=Word(b,format+14);
    if(Word(b,format)!=1 || (channels!=1 && channels!=2) || bits!=16)
        return {{},Error(ServiceErrorCode::UnsupportedFormat,"Use 16-bit PCM mono or stereo WAV.",path)};
    const auto rate=Dword(b,format+4);
    const std::uint64_t align=channels*2;
    if(!rate || Word(b,format+12)!=align || Dword(b,format+8)!=std::uint64_t(rate)*align || dataSize%align)
        return bad("Invalid WAV sample rate, byte rate, block alignment or frame length.");
    const std::uint64_t frames=dataSize/align;
    const auto outputFrames=(frames*16000+rate-1)/rate;
    const auto encodedBytes=((outputFrames+63)/64)*32;
    if(!encodedBytes || encodedBytes>=kBodyLimit) return bad("Converted WAV exceeds PCM bank capacity.");
    Adpcm encoder;
    std::uint32_t size=0;
    std::unique_ptr<std::uint8_t[]> encoded(encoder.waveToAdpcm(b.data(),b.size(),size,16000));
    if(!encoded || size!=encodedBytes) return bad("WAV conversion failed.");
    return {Bytes(encoded.get(),encoded.get()+size),{}};
}
ServiceError Append(PcmBankArtifact &bank,const Bytes &sample,int slot,const std::string &path) {
    if(sample.empty()) return Error(ServiceErrorCode::InvalidData,"PCM sample is empty.",path);
    const auto padded=(sample.size()+3)&~std::size_t(3);
    const auto start=bank.bytes.size()-kHeader;
    if(padded>=kBodyLimit || start>=kBodyLimit-padded)
        return Error(ServiceErrorCode::InvalidData,"PCM bank body must be below 262144 bytes.",path);
    bank.bytes.insert(bank.bytes.end(),sample.begin(),sample.end());
    bank.bytes.resize(kHeader+start+padded,0);
    PutWord(bank.bytes,slot*32+16,start/4); PutWord(bank.bytes,slot*32+18,(start+padded)/4);
    PutWord(bank.bytes,slot*32+28,start/4); PutWord(bank.bytes,slot*32+30,padded/4);
    bank.input_paths.push_back(path);
    return {};
}
ServiceError Validate(const PcmBankArtifact &bank,const std::string &path) {
    const auto &b=bank.bytes;
    if(b.size()<=kHeader || b.size()>=kHeader+kBodyLimit || (b.size()-kHeader)%4)
        return Error(ServiceErrorCode::InvalidData,"Invalid PCM bank size.",path);
    std::size_t end=0;
    for(int slot=0;slot<32;++slot) {
        const std::size_t start=Word(b,slot*32+28)*4, length=Word(b,slot*32+30)*4;
        if(!length) {
            if(start) return Error(ServiceErrorCode::InvalidData,"Empty PCM slot has a start address.",path);
            continue;
        }
        if(start!=end || length>b.size()-kHeader-end)
            return Error(ServiceErrorCode::InvalidData,"Invalid PCM bank entry range.",path);
        end=start+length;
    }
    if(end!=b.size()-kHeader) return Error(ServiceErrorCode::InvalidData,"PCM body contains unreferenced bytes.",path);
    return {};
}
}
ServiceResult<PcmBankArtifact> PcmBankService::BuildFromList(const std::string &path) const {
    const auto input=Read(path,1024*1024);
    if(!input.Succeeded()) return {{},input.error};
    std::string text(input.value.begin(),input.value.end());
    if(text.compare(0,3,"\xef\xbb\xbf")==0) text.erase(0,3);
    DocumentService validation;
    const auto valid=validation.ReplaceText(text);
    if(!valid.Succeeded()) return {{},Error(ServiceErrorCode::InvalidData,"PCM list must be UTF-8 without NUL bytes.",path)};
    PcmBankArtifact bank; bank.bytes.resize(kHeader,0); bank.input_paths.push_back(Absolute(path));
    std::istringstream lines(valid.value.utf8_text); std::string line; int lineNumber=0,slot=0;
    while(std::getline(lines,line)) {
        ++lineNumber;
        if(line.find_first_not_of(" \t")==std::string::npos) continue;
        if(slot==32) return {{},Error(ServiceErrorCode::InvalidData,"PCM list has more than 32 entries.",path)};
        fs::path samplePath(line);
        if(samplePath.is_relative()) samplePath=fs::path(Absolute(path)).parent_path()/samplePath;
        const auto sampleFile=samplePath.lexically_normal().string();
        auto ext=samplePath.extension().string();
        std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return std::tolower(c);});
        ServiceResult<Bytes> sample;
        if(ext!=".wav" && ext!=".adpcm" && ext!=".bin")
            sample.error=Error(ServiceErrorCode::UnsupportedFormat,"Unsupported PCM sample extension.",sampleFile);
        else {
            sample=Read(sampleFile,ext==".wav" ? 16*1024*1024 : kBodyLimit);
            if(sample.Succeeded() && ext==".wav") sample=Wav(sample.value,sampleFile);
        }
        if(sample.Succeeded()) {
            std::fill_n(bank.bytes.begin()+slot*32,16,' ');
            const auto name=samplePath.stem().string();
            for(std::size_t i=0;i<std::min<std::size_t>(16,name.size());++i)
                bank.bytes[slot*32+i]=static_cast<unsigned char>(name[i])<128 ? name[i] : '?';
            sample.error=Append(bank,sample.value,slot,sampleFile);
        }
        if(!sample.Succeeded()) {
            sample.error.message="PCM list line "+std::to_string(lineNumber)+": "+sample.error.message;
            return {{},sample.error};
        }
        ++slot;
    }
    if(!slot) return {{},Error(ServiceErrorCode::InvalidData,"PCM list has no entries.",path)};
    return {std::move(bank),{}};
}
ServiceResult<PcmBankArtifact> PcmBankService::BuildFromDataDirectory(const std::string &path) const {
    if(path.empty() || path.find('\0')!=std::string::npos)
        return {{},Error(ServiceErrorCode::InvalidArgument,"Choose a DATA directory path.",path)};
    const auto directory=fs::path(Absolute(path)); const auto dataPath=(directory/"DATA").string();
    const auto header=Read(dataPath,kHeader);
    if(!header.Succeeded()) return {{},header.error};
    if(header.value.size()!=kHeader) return {{},Error(ServiceErrorCode::InvalidData,"DATA must be exactly 1024 bytes.",dataPath)};
    PcmBankArtifact bank; bank.bytes.resize(kHeader,0); bank.input_paths.push_back(dataPath); int count=0;
    for(int slot=0;slot<32;++slot) {
        const auto at=slot*32; const int low=Word(header.value,at+16), high=Word(header.value,at+18);
        if(high<low) return {{},Error(ServiceErrorCode::InvalidData,"DATA contains reversed addresses.",dataPath)};
        if(high==low) continue;
        const auto voice=(directory/("VOICE._"+std::to_string(slot+1))).string();
        const auto sample=Read(voice,kBodyLimit);
        if(!sample.Succeeded()) return {{},sample.error};
        if(sample.value.size()!=std::size_t(high-low)*4)
            return {{},Error(ServiceErrorCode::InvalidData,"VOICE length does not match its DATA address range.",voice)};
        std::copy_n(header.value.begin()+at,32,bank.bytes.begin()+at);
        if(const auto error=Append(bank,sample.value,slot,voice)) return {{},error};
        ++count;
    }
    if(!count) return {{},Error(ServiceErrorCode::InvalidData,"DATA has no occupied slots.",dataPath)};
    return {std::move(bank),{}};
}
ServiceResult<std::string> PcmBankService::Save(const PcmBankArtifact &bank,const std::string &path) const {
    if(path.empty() || path.find('\0')!=std::string::npos)
        return {{},Error(ServiceErrorCode::InvalidArgument,"Choose a PCM output path.",path)};
    if(const auto error=Validate(bank,path)) return {{},error};
    std::error_code ec;
    const auto destination=fs::weakly_canonical(path,ec);
    if(ec) return {{},Error(ServiceErrorCode::IoError,"Cannot resolve output path.",path)};
    for(const auto &input:bank.input_paths) {
        std::error_code canonicalError,equivalentError;
        const auto source=fs::weakly_canonical(input,canonicalError);
        const bool equivalent=fs::equivalent(input,path,equivalentError);
        if((!canonicalError && source==destination) || (!equivalentError && equivalent))
            return {{},Error(ServiceErrorCode::InvalidArgument,"Choose an output different from all PCM inputs.",path)};
    }
    if(fs::exists(path,ec) && !fs::is_regular_file(path,ec))
        return {{},Error(ServiceErrorCode::IoError,"Output is not a regular file.",path)};
    const auto target=fs::path(Absolute(path));
    auto pattern=(target.parent_path()/".mucom88-pcm-XXXXXX").string();
    std::vector<char> name(pattern.begin(),pattern.end()); name.push_back(0);
    const int fd=mkstemp(name.data());
    if(fd<0) return {{},Error(ServiceErrorCode::IoError,"Cannot create temporary PCM output.",path)};
    const fs::path temporary(name.data());
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ignored; fs::remove(path,ignored); } } cleanup{temporary};
    FILE *file=fdopen(fd,"wb");
    if(!file) { close(fd); return {{},Error(ServiceErrorCode::IoError,"Cannot open temporary PCM output.",path)}; }
    bool ok=fwrite(bank.bytes.data(),1,bank.bytes.size(),file)==bank.bytes.size();
    if(fflush(file)!=0 || fsync(fd)!=0) ok=false;
    if(fclose(file)!=0) ok=false;
    if(!ok) return {{},Error(ServiceErrorCode::IoError,"Cannot write PCM output.",path)};
    fs::rename(temporary,target,ec);
    if(ec) return {{},Error(ServiceErrorCode::IoError,"Cannot replace PCM output.",path)};
    return {path,{}};
}
}
