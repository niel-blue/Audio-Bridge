#define NOMINMAX
#include <windows.h>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "controller.h"
#include "editor.h"
#include "processor.h"
#include "additional_routes.h"
#include "parameters.h"
#include "vst_messages.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "vstgui4/vstgui/lib/cframe.h"
#include "vstgui4/vstgui/lib/coffscreencontext.h"
#include "vstgui4/vstgui/lib/platform/platformfactory.h"
#include "vstgui4/vstgui/lib/vstguiinit.h"

using namespace audio_bridge;
using namespace audio_bridge::vst;
using namespace Steinberg;
using namespace Steinberg::Vst;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool nearly(double a, double b) { return std::abs(a-b)<0.0001; }
state::Data current(Processor* p) {
  MemoryStream stream; check(p->getState(&stream)==kResultTrue,"processor save");
  stream.seek(0,IBStream::kIBSeekSet,nullptr); state::Data result;
  check(state::Read(&stream,result),"processor decode"); return result;
}
void restore(Processor* p, const state::Data& data) {
  MemoryStream stream; check(state::Write(&stream,data),"encode");
  stream.seek(0,IBStream::kIBSeekSet,nullptr);
  check(p->setState(&stream)==kResultTrue,"processor restore");
}
VSTGUI::CControl* find(VSTGUI::CViewContainer* container, int tag) {
  for(uint32_t i=0;i<container->getNbViews();++i) { auto* view=container->getView(i);
    if(auto* c=dynamic_cast<VSTGUI::CControl*>(view); c && c->getTag()==tag) return c;
    if(auto* child=dynamic_cast<VSTGUI::CViewContainer*>(view)) if(auto* c=find(child,tag)) return c;
  }
  return nullptr;
}
void pump(unsigned milliseconds) {
  const auto until=GetTickCount64()+milliseconds;
  do {
    MSG msg;
    while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessage(&msg);}
    Sleep(1);
  } while(GetTickCount64()<until);
}
void delay_edits(VSTGUI::CViewContainer* container,std::vector<VSTGUI::CTextEdit*>& edits) {
  for(uint32_t i=0;i<container->getNbViews();++i) {
    auto* view=container->getView(i);
    if(auto* edit=dynamic_cast<VSTGUI::CTextEdit*>(view);edit && edit->getViewSize().left==186) edits.push_back(edit);
    if(auto* child=dynamic_cast<VSTGUI::CViewContainer*>(view)) delay_edits(child,edits);
  }
}
void snapshot(Editor* editor, const std::string& path) {
  auto ctx=VSTGUI::COffscreenContext::create(VSTGUI::CPoint(300,380),2);
  check(bool(ctx),"offscreen context"); ctx->beginDraw();
  editor->getFrame()->drawRect(ctx,VSTGUI::CRect(0,0,300,380)); ctx->endDraw();
  auto png=VSTGUI::getPlatformFactory().createBitmapMemoryPNGRepresentation(ctx->getBitmap()->getPlatformBitmap());
  check(!png.empty(),"PNG render");
  std::ofstream out(path,std::ios::binary); out.write(reinterpret_cast<const char*>(png.data()),png.size());
  check(out.good(),"PNG save");
}
int main(int argc,char** argv) {
 try {
  CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED); VSTGUI::init(GetModuleHandle(nullptr));
  auto* host=new HostApplication;
  auto* controller=new Controller; auto* processor=new Processor;
  check(controller->initialize(host)==kResultTrue,"controller init");
  check(processor->initialize(host)==kResultTrue,"processor init");
  controller->connect(processor); processor->connect(controller);
  ProcessSetup setup{}; setup.sampleRate=48000; setup.maxSamplesPerBlock=64; setup.symbolicSampleSize=kSample32;
  check(processor->setupProcessing(setup)==kResultTrue,"setup");
  std::array<float,64> left{},right{},out_l{},out_r{}; left.fill(0.1f); right.fill(0.2f);
  float* in_channels[]{left.data(),right.data()}; float* out_channels[]{out_l.data(),out_r.data()};
  AudioBusBuffers in{},out{}; in.numChannels=out.numChannels=2; in.channelBuffers32=in_channels; out.channelBuffers32=out_channels;
  ProcessData data{};data.numInputs=data.numOutputs=1;data.inputs=&in;data.outputs=&out;data.numSamples=64;data.symbolicSampleSize=kSample32;
  auto run=[&] { check(processor->process(data)==kResultTrue,"process"); };
  auto automation=[&](ParamID id,double value) { ParameterChanges changes;int32 index=0;
    auto* q=changes.addParameterData(id,index);q->addPoint(0,value,index);
    data.inputParameterChanges=&changes;run();data.inputParameterChanges=nullptr;
  };
  // Check actual mono/stereo and in-place host buffers, not only stereo scratch arrays.
  for(const bool mono_input : {false,true}) {
    in.numChannels=mono_input ? 1 : 2;
    for(const bool mono_output : {false,true}) {
      out.numChannels=mono_output ? 1 : 2;
      for(int mute=-1;mute<4;++mute) {
        state::Data separate;separate.input_link=separate.output_link=false;
        separate.input_gain_db=6;separate.input_gain_right_db=-6;
        separate.output_gain_db=-3;separate.output_gain_right_db=3;
        separate.input_mute_left=mute==0;separate.input_mute_right=mute==1;
        separate.output_mute_left=mute==2;separate.output_mute_right=mute==3;
        restore(processor,separate);run();
        const double expected_l=(mute==0||mute==2) ? 0 : 0.1*std::pow(10.,3./20);
        const double expected_r=(mute==1||mute==3) ? 0 : (mono_input ? 0.1 : 0.2)*std::pow(10.,-3./20);
        check(nearly(out_l[0],mono_output ? (expected_l+expected_r)*0.5 : expected_l),"L/R gain and mute across host bus widths");
        if(!mono_output) check(nearly(out_r[0],expected_r),"R output remains independent");
        if(mono_output) check((out.silenceFlags&1U)==0,"single-channel mute must not silence mono return");
      }
    }
  }
  in.numChannels=out.numChannels=2;
  {
    state::Data separate;separate.input_link=separate.output_link=false;
    separate.input_mute_left=true;restore(processor,separate);
    out.channelBuffers32=in_channels;run();
    check(left[0]==0&&nearly(right[0],0.2),"in-place host buffers preserve R when L muted");
    out.channelBuffers32=out_channels;left.fill(0.1F);right.fill(0.2F);
    restore(processor,{});
  }
  state::Data s; s.input_link=s.output_link=false;s.input_gain_db=6;s.input_gain_right_db=-6;s.output_gain_db=-3;s.output_gain_right_db=3;
  restore(processor,s);run();
  check(nearly(out_l[0],0.1*std::pow(10.,3./20)) && nearly(out_r[0],0.2*std::pow(10.,-3./20)),"independent LR input and output gain");
  for(int mute=0;mute<4;++mute) {
    auto m=s; m.input_mute_left=mute==0; m.input_mute_right=mute==1;m.output_mute_left=mute==2;m.output_mute_right=mute==3;
    restore(processor,m);run();
    check((mute%2==0 ? out_l[0]==0 && out_r[0]!=0 : out_r[0]==0 && out_l[0]!=0),"individual mute isolation");
  }
  restore(processor,{});
  {
    ParameterChanges changes;int32 index=0;
    auto* q=changes.addParameterData(kInputGain,index);q->addPoint(0,GainDbToNormalized(8),index);
    q=changes.addParameterData(kInputGainRight,index);q->addPoint(0,GainDbToNormalized(8),index);
    data.inputParameterChanges=&changes;run();data.inputParameterChanges=nullptr;
  }
  auto saved=current(processor);check(nearly(saved.input_gain_db,8)&&nearly(saved.input_gain_right_db,8),"linked input values arrive independently at DSP");
  automation(kInputLink,0);automation(kInputGain,GainDbToNormalized(-12));
  saved=current(processor);check(nearly(saved.input_gain_db,-12)&&nearly(saved.input_gain_right_db,8),"unlinked automation");
  automation(kInputLink,1);saved=current(processor);check(nearly(saved.input_gain_right_db,-12),"relink synchronizes R to L");
  {
    ParameterChanges changes;int32 index=0;
    auto* q=changes.addParameterData(kOutputGain,index);q->addPoint(0,GainDbToNormalized(5),index);
    q=changes.addParameterData(kOutputGainRight,index);q->addPoint(0,GainDbToNormalized(5),index);
    data.inputParameterChanges=&changes;run();data.inputParameterChanges=nullptr;
  }
  saved=current(processor);check(nearly(saved.output_gain_db,5)&&nearly(saved.output_gain_right_db,5),"linked output values arrive independently at DSP");
  {
    ParameterChanges changes;int32 index=0;
    auto* q=changes.addParameterData(kInputGain,index);q->addPoint(0,GainDbToNormalized(3),index);
    q=changes.addParameterData(kInputGainRight,index);q->addPoint(0,GainDbToNormalized(-4),index);
    q=changes.addParameterData(kInputLink,index);q->addPoint(0,0,index);
    data.inputParameterChanges=&changes;run();data.inputParameterChanges=nullptr;
    saved=current(processor);check(nearly(saved.input_gain_db,3)&&nearly(saved.input_gain_right_db,-4),"simultaneous unlink and LR automation");
    data.numSamples=0;automation(kInputMuteRight,1);data.numSamples=64;
    check(current(processor).input_mute_right,"zero-buffer parameter flush");
    std::array<float,1> l{1},r{2},hl{},hr{},el{},er{};
    audio::RouteSelectedInputBlock(nullptr,nullptr,l.data(),r.data(),2,3,4,5,hl.data(),hr.data(),el.data(),er.data(),1);
    check(hl[0]==2&&hr[0]==6&&el[0]==8&&er[0]==30,"external output LR gain isolation");
  }
  {
    audio::OutputDelay long_delay;long_delay.Prepare(1000,kMaximumDelayMs);
    std::vector<float> l(10001),r(10001);l[0]=1;r[0]=-0.5F;
    long_delay.Process(l.data(),r.data(),l.size(),10000);
    check(std::all_of(l.begin(),l.end()-1,[](float v){return v==0;})&&l.back()==1&&r.back()==-0.5F,"10000 ms audio delay retains full buffer length");
  }
  // Roundtrip every new field, delays and remembered routes.
  s.input_mute_left=true;s.output_mute_right=true;s.input_delay_ms=12;s.output_delay_ms=23;
  s.input_id="remembered-input";s.output_id="remembered-output";s.exclusive=true;
  MemoryStream stream;check(state::Write(&stream,s),"state write");stream.seek(0,IBStream::kIBSeekSet,nullptr);
  state::Data decoded;check(state::Read(&stream,decoded),"state read");
  check(nearly(decoded.input_gain_right_db,-6)&&nearly(decoded.output_gain_right_db,3)&&!decoded.input_link&&!decoded.output_link&&decoded.input_mute_left&&decoded.output_mute_right&&nearly(decoded.input_delay_ms,12)&&nearly(decoded.output_delay_ms,23)&&decoded.input_id==s.input_id&&decoded.output_id==s.output_id&&decoded.exclusive,"full state roundtrip");
  auto attrs=HostAttributeList::make();check(messages::WriteState(attrs,s,true),"message write");
  check(messages::ReadState(attrs,decoded,true)&&decoded.input_mute_left&&decoded.output_mute_right&&nearly(decoded.input_gain_right_db,-6)&&!decoded.input_link,"message roundtrip");
  // Pre-LR v3 state maps the old gain to both channels with links enabled.
  std::string legacy;const auto append=[&](auto v) { legacy.append(reinterpret_cast<const char*>(&v),sizeof(v)); };
  append(uint32_t(3));append(double(4));append(double(-2));append(double(11));append(double(7));append(uint8_t(0));append(uint32_t(0));append(uint32_t(0));
  MemoryStream old;int32 size=static_cast<int32>(legacy.size());old.write(&size,sizeof(size),nullptr);old.write(legacy.data(),size,nullptr);old.seek(0,IBStream::kIBSeekSet,nullptr);
  check(state::Read(&old,decoded)&&nearly(decoded.input_gain_right_db,4)&&nearly(decoded.output_gain_right_db,-2)&&decoded.input_link&&decoded.output_link,"legacy state migration");
  restore(processor,{});
  controller->setParamNormalized(kInputLink,0);controller->setParamNormalized(kInputGainRight,GainDbToNormalized(-7));
  controller->setParamNormalized(kInputLink,1);check(nearly(controller->getParamNormalized(kInputGainRight),0.5),"controller relink");
  controller->setParamNormalized(kInputGainRight,GainDbToNormalized(9));check(nearly(GainDbFromNormalized(controller->getParamNormalized(kInputGain)),9),"controller host automation link");
  MemoryStream defaults;state::Write(&defaults,{});defaults.seek(0,IBStream::kIBSeekSet,nullptr);controller->setState(&defaults);
  WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandle(nullptr);wc.lpszClassName=L"AudioBridgeControlsTest";RegisterClassW(&wc);
  auto hwnd=CreateWindowW(wc.lpszClassName,L"Audio-Bridge Test",WS_POPUP,0,0,300,380,nullptr,nullptr,wc.hInstance,nullptr);
  auto* editor=new Editor(controller);check(editor->open(hwnd,VSTGUI::PlatformType::kHWND),"editor opens");
  check(editor->getFrame()->getViewSize().getWidth()==300&&editor->getFrame()->getViewSize().getHeight()==380,"fixed editor dimensions");
  // Exercise mouse-down/timer-refresh/mouse-up instead of setting button values directly.
  for(auto id : {kInputMuteLeft,kInputMuteRight,kOutputMuteLeft,kOutputMuteRight}) {
    auto* button=dynamic_cast<VSTGUI::CTextButton*>(find(editor->getFrame(),id));
    check(button!=nullptr,"mute button exists");
    const auto rect=button->getViewSize();
    VSTGUI::CPoint point((rect.left+rect.right)/2,(rect.top+rect.bottom)/2);
    for(unsigned hold : {0U,80U,160U}) {
      for(bool on : {true,false}) {
        button->onMouseDown(point,VSTGUI::CButtonState(VSTGUI::kLButton));
        if(hold) pump(hold);
        button->onMouseUp(point,VSTGUI::CButtonState(VSTGUI::kLButton));
        check((controller->getParamNormalized(id)>0.5)==on,"MUTE click lost across timer refresh");
        run();
        const bool mute_left=id==kInputMuteLeft||id==kOutputMuteLeft;
        check(on ? (mute_left ? out_l[0]==0&&out_r[0]!=0 : out_r[0]==0&&out_l[0]!=0)
                 : (out_l[0]!=0&&out_r[0]!=0),"MUTE mouse click reaches audio channel");
      }
    }
    button->onMouseDown(point,VSTGUI::CButtonState(VSTGUI::kLButton));pump(80);button->onMouseCancel();
    check(controller->getParamNormalized(id)==0,"cancelled MUTE does not commit");
  }
  editor->getFrame()->onActivate(true);
  std::vector<VSTGUI::CTextEdit*> delay_fields;delay_edits(editor->getFrame(),delay_fields);
  check(delay_fields.size()==2,"input and output delay entry fields");
  for(size_t i=0;i<delay_fields.size();++i) {
    auto* field=delay_fields[i];const auto id=i==0 ? kInputDelay : kOutputDelay;
    check(!(field->getStyle()&VSTGUI::CParamDisplay::kNoFrame),"delay input visibly framed");
    for(const auto& entry : std::vector<std::pair<const char*,double>>{{"123.4",123.4},{"456.7 ms",456.7},{"1000",1000},{"2500",2500},{"invalid",2500},{"10000",10000},{"12000",10000},{"-1",0}}) {
      field->takeFocus();auto native=field->getPlatformTextEdit();check(bool(native),"native delay field opens");
      native->setText(entry.first);pump(80);
      check(native->getText()==entry.first,"polling preserves typed delay");
      editor->getFrame()->setFocusView(nullptr);
      const auto state=current(processor);
      check(nearly(i==0 ? state.input_delay_ms : state.output_delay_ms,entry.second),"typed delay committed to processor");
      check(nearly(DelayMsFromNormalized(controller->getParamNormalized(id)),entry.second),"typed delay controller value");
      pump(60);
      auto* knob=find(editor->getFrame(),id);
      check(nearly(knob->getMax(),1000),"delay knob max is 1000 ms");
      check(nearly(knob->getValue(),std::min(entry.second,1000.0)),"knob display clamps independently of typed delay");
      double displayed=0;
      check(ui::ParseValue(field->getText().getString(),"ms",0,kMaximumDelayMs,displayed)&&nearly(displayed,entry.second),"large typed delay remains visible after refresh");
      MemoryStream saved_delay;controller->getState(&saved_delay);saved_delay.seek(0,IBStream::kIBSeekSet,nullptr);controller->setState(&saved_delay);
      check(nearly(i==0 ? current(processor).input_delay_ms : current(processor).output_delay_ms,entry.second),"large delay survives save and restore");

    }
  }
  // Every gain/delay knob uses vertical relative motion, regardless of host mode.
  for(auto mode : {Steinberg::Vst::kCircularMode,Steinberg::Vst::kRelativCircularMode,Steinberg::Vst::kLinearMode}) {
    controller->setKnobMode(mode);
    for(auto id : {kInputGain,kInputGainRight,kOutputGain,kOutputGainRight,kInputDelay,kOutputDelay}) {
      const bool delay=id==kInputDelay||id==kOutputDelay;
      auto* knob=dynamic_cast<ui::RotaryKnob*>(find(editor->getFrame(),id));
      check(knob!=nullptr,"rotary control exists");
      for(bool alt : {false,true}) {
        const float initial=delay ? 500.0F : 0.0F;
        knob->setValue(initial);
        const auto r=knob->getViewSize();
        VSTGUI::CPoint origin(r.left+7,r.top+10),point=origin;
        const VSTGUI::CButtonState buttons(VSTGUI::kLButton|(alt ? VSTGUI::kAlt : 0));
        knob->onMouseDown(point,buttons);
        check(nearly(knob->getValue(),initial),"no value jump on grab");
        point.x+=30;knob->onMouseMoved(point,buttons);
        check(nearly(knob->getValue(),initial),"horizontal motion has no effect");
        point.y=origin.y-12;knob->onMouseMoved(point,buttons);
        check(knob->getValue()>initial,"upward drag increases value");
        const auto raised=knob->getValue();
        point.x-=60;knob->onMouseMoved(point,buttons);
        check(nearly(knob->getValue(),raised),"sideways motion preserves raised value");
        point.y=origin.y+12;knob->onMouseMoved(point,buttons);
        check(knob->getValue()<initial,"downward drag decreases value");
        knob->onMouseUp(point,buttons);
        const auto state=current(processor);
        const double actual=id==kInputGain ? state.input_gain_db : id==kInputGainRight ? state.input_gain_right_db : id==kOutputGain ? state.output_gain_db : id==kOutputGainRight ? state.output_gain_right_db : id==kInputDelay ? state.input_delay_ms : state.output_delay_ms;
        check(nearly(actual,knob->getValue()),"vertical drag reaches audio processor");
      }
    }
  }
  MemoryStream after_drag;state::Write(&after_drag,{});after_drag.seek(0,IBStream::kIBSeekSet,nullptr);controller->setState(&after_drag);pump(60);
  const auto ui_edit=[&](ParamID id,float value) { auto* c=find(editor->getFrame(),id);check(c!=nullptr,"UI control exists");c->setValue(value);editor->valueChanged(c); };
  ui_edit(kInputGain,6);saved=current(processor);check(nearly(saved.input_gain_db,6)&&nearly(saved.input_gain_right_db,6),"UI linked input gain");
  ui_edit(kInputLink,0);ui_edit(kInputGainRight,-8);ui_edit(kInputMuteLeft,1);saved=current(processor);
  check(nearly(saved.input_gain_db,6)&&nearly(saved.input_gain_right_db,-8)&&saved.input_mute_left&&!saved.input_mute_right,"UI unlink and mute");
  ui_edit(kOutputLink,0);ui_edit(kOutputGainRight,4);ui_edit(kOutputMuteRight,1);ui_edit(kInputDelay,10);ui_edit(kOutputDelay,20);
  saved=current(processor);check(nearly(saved.output_gain_right_db,4)&&saved.output_mute_right&&nearly(saved.input_delay_ms,10)&&nearly(saved.output_delay_ms,20),"UI output and shared delays");
  MemoryStream ui_state;controller->getState(&ui_state);ui_state.seek(0,IBStream::kIBSeekSet,nullptr);controller->setState(&ui_state);
  auto restored=current(processor);check(nearly(restored.input_gain_right_db,-8)&&restored.input_mute_left&&restored.output_mute_right&&!restored.input_link&&!restored.output_link,"UI save restore");
  // Restore defaults for the delivered design preview.
  MemoryStream reset;state::Write(&reset,{});reset.seek(0,IBStream::kIBSeekSet,nullptr);controller->setState(&reset);
  MSG msg;for(int i=0;i<10;++i){while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessage(&msg);}Sleep(10);}
  if(argc>1) snapshot(editor,argv[1]);
  editor->close();editor->release();DestroyWindow(hwnd);
  processor->disconnect(controller);controller->disconnect(processor);
  processor->terminate();controller->terminate();processor->release();controller->release();host->release();
  VSTGUI::exit();CoUninitialize();
  std::cout<<"LR DSP, automation, mute, state migration, message sync, editor controls and rendering: PASS\n";return 0;
 } catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
}

void* moduleHandle = GetModuleHandle(nullptr);
