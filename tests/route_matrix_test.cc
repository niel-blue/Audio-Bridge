#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include "processor.h"
#include "parameters.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

namespace audio_bridge::audio {
std::array<float,64> captured_left{},captured_right{};
class AudioIo::Impl {public:bool input=false,output=false;};
AudioIo::AudioIo(StatusCallback):impl_(std::make_unique<Impl>()){}
AudioIo::~AudioIo()=default;
void AudioIo::Start(const Config& config){impl_->input=!config.input_id.empty();impl_->output=!config.output_id.empty();}
void AudioIo::Stop() noexcept {impl_->input=impl_->output=false;}
void AudioIo::PopInputBlock(float* l,float* r,std::size_t n) noexcept {std::fill_n(l,n,.3F);std::fill_n(r,n,.4F);}
void AudioIo::PushOutputBlock(const float* l,const float* r,std::size_t n) noexcept {std::copy_n(l,n,captured_left.data());std::copy_n(r,n,captured_right.data());}
bool AudioIo::InputEnabled()const noexcept{return impl_->input;}
bool AudioIo::OutputEnabled()const noexcept{return impl_->output;}
Status AudioIo::GetStatus()const noexcept{return Status::kActive;}
}
void* moduleHandle=nullptr;
int main(){
 using namespace Steinberg;using namespace Steinberg::Vst;using namespace audio_bridge;
 const auto check=[](bool b){if(!b)throw std::runtime_error("route matrix mismatch");};
 const auto near=[](double a,double b){return std::abs(a-b)<.0001;};
 try{
  auto* host=new HostApplication;auto* processor=new vst::Processor;check(processor->initialize(host)==kResultTrue);
  ProcessSetup setup{};setup.sampleRate=48000;setup.maxSamplesPerBlock=64;setup.symbolicSampleSize=kSample32;check(processor->setupProcessing(setup)==kResultTrue);
  std::array<float,64> hl{},hr{},ol{},orr{};hl.fill(.1F);hr.fill(.2F);float* ip[]{hl.data(),hr.data()};float* op[]{ol.data(),orr.data()};
  AudioBusBuffers in{},out{};in.channelBuffers32=ip;out.channelBuffers32=op;
  ProcessData data{};data.numSamples=64;data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;data.symbolicSampleSize=kSample32;
  int cases=0;
  for(bool ext_in : {false,true})for(bool ext_out : {false,true})for(int input_channels : {1,2})for(int output_channels : {1,2})for(bool linked : {false,true})for(int mute=-1;mute<4;++mute){
   state::Data s;s.input_link=s.output_link=linked;s.input_gain_db=6;s.input_gain_right_db=-6;s.output_gain_db=-3;s.output_gain_right_db=3;
   s.input_mute_left=mute==0;s.input_mute_right=mute==1;s.output_mute_left=mute==2;s.output_mute_right=mute==3;
   s.input_id=ext_in ? "test-input" : "";s.output_id=ext_out ? "test-output" : "";
   MemoryStream stream;check(state::Write(&stream,s));stream.seek(0,IBStream::kIBSeekSet,nullptr);check(processor->setState(&stream)==kResultTrue);check(processor->setActive(true)==kResultTrue);
   in.numChannels=input_channels;out.numChannels=output_channels;check(processor->process(data)==kResultTrue);
   double l=mute==0 ? 0 : (ext_in ? .3 : .1)*std::pow(10.,6./20);
   double r=mute==1 ? 0 : (ext_in ? .4 : input_channels==1 ? .1 : .2)*std::pow(10.,(linked ? 6. : -6.)/20);
   double post_l=mute==2 ? 0 : l*std::pow(10.,-3./20);
   double post_r=mute==3 ? 0 : r*std::pow(10.,(linked ? -3. : 3.)/20);
   const auto host_l=ext_out ? l : post_l;const auto host_r=ext_out ? r : post_r;
   check(near(ol[0],output_channels==1 ? (host_l+host_r)*.5 : host_l));if(output_channels==2)check(near(orr[0],host_r));
   if(output_channels==2&&mute>=0)check(out.silenceFlags==0);
   if(ext_out){check(near(audio::captured_left[0],post_l));check(near(audio::captured_right[0],post_r));}
   ++cases;
  }
  {
   processor->setActive(false);
   state::Data linked;linked.input_link=linked.output_link=true;
   MemoryStream stream;check(state::Write(&stream,linked));stream.seek(0,IBStream::kIBSeekSet,nullptr);
   check(processor->setState(&stream)==kResultTrue);
   check(processor->setActive(true)==kResultTrue);
   in.numChannels=out.numChannels=2;
   hl.fill(.1F);hr.fill(.2F);
   ParameterChanges changes;int32 queue_index=0;
   auto* queue=changes.addParameterData(vst::kInputGainRight,queue_index);
   check(queue!=nullptr&&queue->addPoint(0,vst::GainDbToNormalized(-6),queue_index)==kResultTrue);
   data.inputParameterChanges=&changes;check(processor->process(data)==kResultTrue);data.inputParameterChanges=nullptr;
   check(near(ol[0],.1)&&near(orr[0],.2*std::pow(10.,-6./20.)));

   MemoryStream reset;check(state::Write(&reset,linked));reset.seek(0,IBStream::kIBSeekSet,nullptr);
   check(processor->setState(&reset)==kResultTrue);
   hl.fill(.1F);hr.fill(.2F);
   ParameterChanges output_changes;queue_index=0;
   queue=output_changes.addParameterData(vst::kOutputGainRight,queue_index);
   check(queue!=nullptr&&queue->addPoint(0,vst::GainDbToNormalized(-6),queue_index)==kResultTrue);
   data.inputParameterChanges=&output_changes;check(processor->process(data)==kResultTrue);data.inputParameterChanges=nullptr;
   check(near(ol[0],.1)&&near(orr[0],.2*std::pow(10.,-6./20.)));
  }
  processor->setActive(false);processor->terminate();processor->release();host->release();
  std::cout<<"PASS "<<cases<<" route cases plus stale LINK isolation for parameter automation.\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
