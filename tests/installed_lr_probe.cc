#define NOMINMAX
#include <windows.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/smartpointer.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "io_state.h"
using namespace Steinberg;
using namespace Steinberg::Vst;
void* moduleHandle=GetModuleHandleW(nullptr);
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
bool near_value(double a,double b){return std::abs(a-b)<0.0001;}
std::wstring wide(const char* value){int n=MultiByteToWideChar(CP_UTF8,0,value,-1,nullptr,0);std::wstring result(n,0);MultiByteToWideChar(CP_UTF8,0,value,-1,result.data(),n);result.resize(n-1);return result;}
int main(int argc,char** argv){
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 try{
  const bool signal=argc>1&&std::string(argv[1])=="--signal";
  const bool inspect=signal||(argc>1&&std::string(argv[1])=="--inspect");
  for(int arg=inspect ? 2 : 1;arg<(signal ? 3 : argc);++arg){
   std::string path=argv[arg];auto full=wide((path+"/Contents/x86_64-win/"+path.substr(path.find_last_of("/\\")+1)).c_str());
   if(std::filesystem::is_regular_file(std::filesystem::path(wide(path.c_str())))) full=wide(path.c_str()); else for(const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(wide(path.c_str())) / L"Contents" / L"x86_64-win")) if(entry.is_regular_file() && entry.path().extension()==L".vst3") full=entry.path().wstring();
   std::cout << "Inspecting " << path << std::endl; auto library=LoadLibraryExW(full.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);check(library!=nullptr,"load module");
   auto init=reinterpret_cast<bool(*)()>(GetProcAddress(library,"InitDll"));if(init)check(init(),"InitDll");
   auto get_factory=reinterpret_cast<IPluginFactory*(*)()>(GetProcAddress(library,"GetPluginFactory"));check(get_factory!=nullptr,"factory export");
   auto* factory=get_factory();auto* host=new HostApplication;
   bool found=false;
   for(int index=0;index<factory->countClasses();++index){
    PClassInfo info{};factory->getClassInfo(index,&info);if(std::string(info.category)!=kVstAudioEffectClass)continue;
    IComponent* component=nullptr;check(factory->createInstance(info.cid,IComponent::iid,reinterpret_cast<void**>(&component))==kResultTrue,"create component");
    check(component->initialize(host)==kResultTrue,"initialize component");
    BusInfo in{},out{};component->getBusInfo(kAudio,kInput,0,in);component->getBusInfo(kAudio,kOutput,0,out);
    std::cout<<info.name<<" input="<<in.channelCount<<" output="<<out.channelCount<<" path="<<path<<"\n";
    if(signal){
     FUnknownPtr<IAudioProcessor> processor(component);check(bool(processor),"audio processor");
     std::ifstream file(argv[3],std::ios::binary);std::vector<char> state_bytes((std::istreambuf_iterator<char>(file)),{});check(!state_bytes.empty(),"saved state file");
     ProcessSetup setup{};setup.sampleRate=48000;setup.maxSamplesPerBlock=256;setup.symbolicSampleSize=kSample32;
     check(processor->setupProcessing(setup)==kResultTrue,"signal setup");
     std::array<float,256> l{},r{},ol{},orr{};float* ip[]{l.data(),r.data()};float* op[]{ol.data(),orr.data()};
     AudioBusBuffers ib{},ob{};ib.numChannels=in.channelCount;ob.numChannels=out.channelCount;ib.channelBuffers32=ip;ob.channelBuffers32=op;
     ProcessData data{};data.numSamples=256;data.symbolicSampleSize=kSample32;data.numInputs=data.numOutputs=1;data.inputs=&ib;data.outputs=&ob;
     for(int channel : {0,1}){
      MemoryStream stream(state_bytes.data(),state_bytes.size());check(component->setState(&stream)==kResultTrue,"load saved signal state");
      MemoryStream verify;component->getState(&verify);verify.seek(0,IBStream::kIBSeekSet,nullptr);audio_bridge::state::Data actual;audio_bridge::state::Read(&verify,actual);std::cout<<"STATE inputL="<<actual.input_gain_db<<" inputR="<<actual.input_gain_right_db<<" outputL="<<actual.output_gain_db<<" outputR="<<actual.output_gain_right_db<<std::endl;
      check(component->setActive(true)==kResultTrue,"signal activate");processor->setProcessing(true);
      double energy_l=0,energy_r=0;int count=0;
      for(int block=0;block<600;++block){
       for(int i=0;i<256;++i){float value=.1F*std::sin(2*3.141592653589793*440*(block*256+i)/48000);l[i]=channel==0 ? value : 0;r[i]=channel==1 ? value : 0;}
       check(processor->process(data)==kResultTrue,"signal processing");
       if(block>400)for(int i=0;i<256;++i){energy_l+=ol[i]*ol[i];energy_r+=orr[i]*orr[i];++count;}
      }
      std::cout<<"SIGNAL "<<(channel==0 ? "L_ONLY" : "R_ONLY")<<" rmsL="<<std::sqrt(energy_l/count)<<" rmsR="<<std::sqrt(energy_r/count)<<std::endl;
      processor->setProcessing(false);component->setActive(false);
     }
    }
    if(!inspect){
     FUnknownPtr<IAudioProcessor> processor(component);check(bool(processor),"audio processor");
     for(int inputs : {1,2})for(int outputs : {1,2}){
      auto in_arr=inputs==1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
      auto out_arr=outputs==1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
      check(processor->setBusArrangements(&in_arr,1,&out_arr,1)==kResultTrue,"bus arrangement");
      ProcessSetup setup{};setup.sampleRate=48000;setup.maxSamplesPerBlock=64;setup.symbolicSampleSize=kSample32;
      check(processor->setupProcessing(setup)==kResultTrue,"setup processing");
      std::array<float,64> l{},r{},ol{},orr{};l.fill(.1F);r.fill(.2F);float* ip[]{l.data(),r.data()};float* op[]{ol.data(),orr.data()};
      AudioBusBuffers ib{},ob{};ib.numChannels=inputs;ob.numChannels=outputs;ib.channelBuffers32=ip;ob.channelBuffers32=op;
      ProcessData data{};data.numSamples=64;data.symbolicSampleSize=kSample32;data.numInputs=data.numOutputs=1;data.inputs=&ib;data.outputs=&ob;
      for(int mute=-1;mute<4;++mute){
       audio_bridge::state::Data state;state.input_link=state.output_link=false;
       state.input_gain_db=5.5;state.input_gain_right_db=3.5;
       state.input_mute_left=mute==0;state.input_mute_right=mute==1;state.output_mute_left=mute==2;state.output_mute_right=mute==3;
       MemoryStream stream;audio_bridge::state::Write(&stream,state);stream.seek(0,IBStream::kIBSeekSet,nullptr);check(component->setState(&stream)==kResultTrue,"restore state");
       check(processor->process(data)==kResultTrue,"process");
       const auto left=mute==0||mute==2 ? 0 : .1*std::pow(10.,5.5/20.);
       const auto right=mute==1||mute==3 ? 0 : (inputs==1 ? .1 : .2)*std::pow(10.,3.5/20.);
       check(near_value(ol[0],outputs==1 ? (left+right)*.5 : left),"installed L/mono mismatch");
       if(outputs==2)check(near_value(orr[0],right),"installed R mismatch");
      }
     }
     std::cout<<"INSTALLED_BINARY_LR_PASS 4 bus arrangements x 5 mute/gain cases\n";
    }
    component->terminate();component->release();found=true;break;
   }
   check(found,"audio class");host->release();factory->release();auto exit=reinterpret_cast<bool(*)()>(GetProcAddress(library,"ExitDll"));if(exit)exit();FreeLibrary(library);
  }
  CoUninitialize();return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
