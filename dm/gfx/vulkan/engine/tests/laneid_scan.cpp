// 全库离线跑 warp32 改写 + lane 号改走 ballot（生产改写器），按 XGS1 公开容器读取，不链接 Vulkan、不改源库；每个改过的模块写出 .spv 供 spirv-val。
// Offline library runner for the production warp32 + lane-id-through-ballot rewriters, using the public XGS1 container layout without Vulkan or source-library writes;
// every changed module is written out as .spv for spirv-val.
// clang++ -std=c++17 tests/laneid_scan.cpp xbg_spirv_warp.cpp xbg_spirv_laneid.cpp -o <仓外产物>; usage: laneid_scan <xgs-dir> <subgroup-size> <new-output-dir>
#include "../xbg_spirv_warp.h"
#include "../xbg_spirv_laneid.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
// 一审 A2：lane 号读取落在入口函数还是辅助函数（辅助函数可能在分歧控制流里被部分 lane 调用），以及函数 / 调用数 | first audit A2: whether lane-id reads sit in entry or helper functions
struct LaneUse{size_t entry_loads=0,helper_loads=0,functions=0,calls=0;};
static LaneUse lane_use(const std::vector<uint32_t> &w){
    LaneUse u;if(w.size()<5)return u;std::vector<bool> entry(w[3],false);uint32_t lane=0,fn=0;
    for(size_t i=5;i<w.size();){uint32_t wc=w[i]>>16,op=w[i]&0xffff;if(!wc||i+wc>w.size())break;
        if(op==15&&wc>=3&&w[i+2]<entry.size())entry[w[i+2]]=true;
        if(op==71&&wc==4&&w[i+2]==11&&w[i+3]==41)lane=w[i+1];
        if(op==54){fn=w[i+2];++u.functions;}
        if(op==57)++u.calls;
        if(op==61&&wc>=4&&lane&&w[i+3]==lane){if(fn<entry.size()&&entry[fn])++u.entry_loads;else ++u.helper_loads;}
        i+=wc;}
    return u;}
int main(int argc,char **argv){try{
    if(argc!=4){std::cerr<<"usage: laneid_scan <xgs-directory> <subgroup-size> <private-output-directory>\n";return 2;}
    unsigned subgroup=unsigned(std::stoul(argv[2]));std::filesystem::path input=argv[1],output=argv[3];
    if(!std::filesystem::is_directory(input))throw std::runtime_error("input is not a directory");
    if(std::filesystem::exists(output))throw std::runtime_error("output must be new, refusing to overwrite evidence");
    std::filesystem::create_directories(output/"spv");
    std::ofstream csv(output/"modules.csv");csv<<"file,stage,warp_sites,lane_loads,entry_lane_loads,helper_lane_loads,functions,calls,input_words,output_words\n";
    std::vector<std::filesystem::path> files;for(auto &e:std::filesystem::directory_iterator(input))if(e.is_regular_file() && e.path().extension()==".xgs")files.push_back(e.path());
    std::sort(files.begin(),files.end());unsigned compute=0,warped=0,laned=0,both=0,with_helper=0;size_t loads=0;
    for(auto &path:files){
        std::ifstream f(path,std::ios::binary|std::ios::ate);auto size=f.tellg();if(size<32 || size>64*1024*1024)throw std::runtime_error("invalid XGS file extent");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));f.seekg(0);f.read(reinterpret_cast<char*>(bytes.data()),size);if(!f)throw std::runtime_error("short XGS read");
        uint32_t h[8];std::memcpy(h,bytes.data(),32);const uint64_t offset=32+uint64_t(h[3])*32+uint64_t(h[4])*4+h[5];
        if(h[0]!=0x31534758 || h[1]!=1 || h[2]>=6 || h[3]>256 || h[4]>16 || h[5]>65536 || h[6]%4 || offset+h[6]!=bytes.size())throw std::runtime_error("invalid XGS1 header/extent");
        if(h[2]!=5)continue;++compute;
        std::vector<uint32_t> w(h[6]/4),out;std::memcpy(w.data(),bytes.data()+offset,h[6]);const size_t in_words=w.size();
        size_t sites=xbg_spirv_warp32(w.data(),w.size(),subgroup,out);if(sites){w.swap(out);++warped;}
        LaneUse use=lane_use(w);if(use.helper_loads)++with_helper;
        size_t k=xbg_spirv_laneid_ballot(w.data(),w.size(),out);if(k){w.swap(out);++laned;loads+=k;if(sites)++both;}
        csv<<path.filename().string()<<','<<h[2]<<','<<sites<<','<<k<<','<<use.entry_loads<<','<<use.helper_loads<<','<<use.functions<<','<<use.calls<<','<<in_words<<','<<w.size()<<'\n';
        if(k){std::ofstream o(output/"spv"/(path.stem().string()+".spv"),std::ios::binary);o.write(reinterpret_cast<const char*>(w.data()),std::streamsize(w.size()*4));}
    }
    std::cout<<"compute="<<compute<<" warp32_changed="<<warped<<" laneid_changed="<<laned<<" both="<<both<<" lane_loads="<<loads<<" modules_with_helper_lane_loads="<<with_helper<<"\n";return 0;
}catch(const std::exception &e){std::cerr<<"laneid_scan: "<<e.what()<<"\n";return 1;}}
