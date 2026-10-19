#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
namespace open_limiter_research {
// Eight-style DSP graph, ported from the independently validated research engine.
// No vendor plugin or captured audio/control/state streams are used. See ENGINE.md.
struct Slot {int level,source,mode;};
struct Target {float base,low,high;bool globalOnly,clamp;std::vector<Slot> slots;};
struct Preset {std::string name;std::vector<Target> targets;std::map<std::string,int> ints;};
#include "engine_presets.h"
constexpr float eps=0x1p-23f;
inline float lin(float db){return (float)std::exp((double)db*0.1151292546497023);}
inline float db(float x){return std::max(-180.f,20.f*std::log10(std::max(std::abs(x),1e-10f)));}
struct Ring {
 std::vector<float> v;int mask=0,pos=0;
 void prepare(int size){int n=1;while(n<size)n*=2;v.assign(n,0);mask=n-1;pos=0;}
 void write(float x){v[pos]=x;}
 float past(int delay)const{return delay>mask?0.f:v[(pos-std::max(0,delay))&mask];}
 void next(){pos=(pos+1)&mask;}
 float step(float x,int delay){write(x);float y=past(delay);next();return y;}
};
struct Maximum {
 Ring r;std::vector<float> tree;
 struct Entry {int64_t time;float value;};std::vector<Entry> queue;
 int head=0,count=0,lastWindow=-1;int64_t time=0;bool stableWindow=false;
 void prepare(int n,bool stable){r.prepare(n);stableWindow=stable;if(stable)queue.resize(r.v.size());else tree.assign(r.v.size()*2,0);}
 void push(float x,int64_t t){while(count&&queue[(head+count-1)&r.mask].value<=x)--count;queue[(head+count)&r.mask]={t,x};++count;}
 float range(int a,int b){float m=0;int n=(int)r.v.size();for(a+=n,b+=n;a<b;a/=2,b/=2){if(a&1)m=std::max(m,tree[a++]);if(b&1)m=std::max(m,tree[--b]);}return m;}
 float step(float x,int h){
  x=std::abs(x);int j=r.pos,n=(int)r.v.size();h=std::clamp(h,0,n-1);
  if(stableWindow){
   if(lastWindow!=h){head=count=0;for(int k=h;k>0;--k)push(r.past(k),time-k);lastWindow=h;}
   while(count&&queue[head].time<time-h){head=(head+1)&r.mask;--count;}
   push(x,time++);r.write(x);r.next();return queue[head].value;
  }
  r.write(x);tree[n+j]=x;
  // Unchanged parent maxima imply all their ancestors are unchanged as well.
  for(int k=(n+j)/2;k;k/=2){float value=std::max(tree[k*2],tree[k*2+1]);if(value==tree[k])break;tree[k]=value;}
  int start=(j-h)&r.mask;float y=start<=j?range(start,j+1):std::max(range(start,n),range(0,j+1));r.next();return y;
 }
};
struct Event {int64_t time;float amount;};
struct Clipper {
 Ring ring;std::vector<Event> events;int head=0,size=0,eventMask=0;Event previous{0,0};int64_t count=0;float prevRaw=0,prevAmount=0;int prevDirection=0,delay=0;
 void prepare(int d){delay=d;ring.prepare(d+2);int n=1;while(n<d+8)n*=2;events.assign(n,{0,0});eventMask=n-1;}
 float step(float raw,float threshold,bool domain){
  float shaped;if(domain)shaped=std::max(0.f,raw-threshold);else{float limit=lin(threshold);shaped=1.f-limit/std::max(std::abs(raw),limit);}
  float delayed=ring.step(shaped,delay);bool near=std::abs(raw-prevRaw)<=eps,falling=raw<prevRaw;int direction=falling?2:(int)near;
  bool changed=(falling||near)&&direction!=prevDirection&&prevDirection!=2;int64_t position=count-delay;bool add=changed;
  if(size>0&&events[head].time<position){previous=events[head];head=(head+1)&eventMask;--size;add=changed||(size==0&&!changed&&previous.amount!=0);}
  if(add){if(size==0&&changed)previous={position-1,0};if(size>eventMask)throw std::runtime_error("Clipper queue capacity");events[(head+size)&eventMask]={count-1,prevAmount};++size;}
  float curve=0;if(size>0){auto b=events[head];float numerator=(b.amount-previous.amount)*(float)(position-previous.time);curve=previous.amount+numerator/(float)(b.time-previous.time);}
  prevAmount=shaped;prevRaw=raw;prevDirection=direction;++count;return std::max(curve,delayed);
 }
};
struct State {
 Ring ring,audio,gain,metadata;Maximum maximum;Clipper clip;
 double envelope=0;float lastA=0,lastR=0;double alphaA=1,alphaR=1;
 double sum=0;int window=0;std::vector<float> kernel;std::array<float,4> kernelSettings{-1,-1,-1,-1};
 double z1=0,z2=0;float b0=1,b1=0,b2=0,a1=0,a2=0;bool filterReady=false;double hpz1=0,hpz2=0;float hpb0=1,hpb1=0,hpb2=0,hpa1=0,hpa2=0;bool hpEnabled=false;
};
class Engine {
 Preset preset;float rate,gainDb,attackMs,releaseMs,aheadMs,ceiling;int64_t sample=0;
 std::array<int,6> detectorDomains{},detectorPurposes{},detectorModes{},vcaDomains{},vcaConvolvers{},vcaClippers{},convolverShapes{},converterModes{},transferModes{},operatorModes{};int aheadStages=0;
 std::array<std::array<float,108>,2> sourceValues{};std::array<std::array<uint8_t,108>,2> sourceFlags{};
 std::array<std::array<float,405>,2> localValues{};std::array<std::array<uint8_t,405>,2> localFlags{};
 std::array<float,405> globalValues{};std::array<uint8_t,405> globalFlags{};
 std::array<std::array<State,108>,2> states;
 std::array<float,2> input{},trigger{};
 float transientLink=1,releaseLink=1;std::array<std::array<float,6>,2> netGains{};
 int parameter(const std::string& s,int fallback=0)const{auto i=preset.ints.find(s);return i==preset.ints.end()?fallback:i->second;}
 static float knee(float delta,float width){float half=width*.5f;if(width>eps&&delta>=-half&&delta<=half){float u=(delta+half)/width;return half*(u*u);}return delta;}
 static double windowValue(int j,int length,int shape,float asym,float power,float amount){
  if(length<2)return 1;double position=(j+1.)/(length+1.),u;float center=1-asym;
  if(std::abs(center)<=eps||std::abs(center-1)<=eps)u=(position+1.-center)*.5;
  else if(position<center)u=.5*position/center;else u=.5+.5*(position-center)/(1.-center);
  double base;if(shape==0)base=.5-std::abs(u-.5)*amount;else if(shape==1)base=1.+(u-u*u-.25)*(float)(amount*4);else if(shape==2)base=.5*(std::cos((u-.5)*amount*2*M_PI)+1);else base=std::abs(amount)<=eps?1:std::min((double)amount,(.5-std::abs(u-.5))*2/amount);
  return std::pow(std::max(0.,base),power*3.+1.);
 }
 float globalSource(int id){if(id>=2&&id<=7){int j=id-2;if(preset.targets[j].globalOnly)return target(j,0,true);return std::max(target(j,0),target(j,1));}throw std::runtime_error("Unexpected global source "+std::to_string(id));}
 float target(int id,int ch,bool global=false){
  if(id<0||id>=405)throw std::runtime_error("Target bounds");
  auto& flags=global?globalFlags:localFlags[ch];auto& values=global?globalValues:localValues[ch];
  if(flags[id]==2)return values[id];if(flags[id]==1)throw std::runtime_error("Target cycle "+std::to_string(id));flags[id]=1;
  const auto& t=preset.targets[id];float v;
  if(global){v=t.base;if(id==0)v=-gainDb;else if(id==2)v=attackMs*.001f;else if(id==3)v=releaseMs*.001f;else if(id==4)v=aheadMs*.2f;
   for(auto s:t.slots)if(s.mode==1){float a=globalSource(s.source),b=target(s.level,ch,true);v=v+a*b;}
   if(t.globalOnly&&t.clamp)v=std::clamp(v,t.low,t.high);
  }else{v=target(id,ch,true);if(!t.globalOnly){for(auto s:t.slots)if(s.mode==2){float a=source(s.source,ch),b=target(s.level,ch);v=v+a*b;}if(t.clamp)v=std::clamp(v,t.low,t.high);}}
  values[id]=v;flags[id]=2;return v;
 }
 float capacitor(float x,int ch,int index){
  auto& s=states[ch][62+index];int t=157+index*8;double delta=(double)x-s.envelope;bool rising=delta>-eps;
  float duration=target(t+(rising?1:4),ch),curve=target(t+(rising?2:5),ch),smooth=target(t+(rising?3:6),ch);
  if(std::abs(curve-1)>eps){float mixed=(float)(delta*duration*(rising?.1:-.1));duration=(duration-mixed)*curve+mixed;}
  if(std::abs(delta)<10&&std::abs(smooth)>eps)duration=(float)(duration+std::exp(-std::min(std::abs(delta),25.))*smooth);
  float samples=duration*rate;float& last=rising?s.lastA:s.lastR;double& alpha=rising?s.alphaA:s.alphaR;
  if(samples!=last){last=samples;alpha=samples>eps?1.-std::exp((float)(-2.3025851249694824f/samples)):1.;}
  s.envelope=((s.envelope+delta*alpha)+1e-16)-1e-16;return (float)s.envelope;
 }
 float detector(int ch,int index){
  int t=49+index*8;float x=target(t,ch),other=target(t,1-ch);float peak=std::max(std::abs(x),std::abs(other));float linking=(index==0||(preset.name=="Dynamic"&&index==2))?transientLink:releaseLink;x=(1-linking)*std::abs(x)+linking*peak;
  int domain=detectorDomains[index],logic=detectorPurposes[index],mode=detectorModes[index];
  float threshold=target(t+4,ch);if(mode==2)return states[ch][32+index].clip.step(domain?db(x):x,threshold,domain);
  float width=target(t+2,ch),ratio=target(t+6,ch),limit=target(t+7,ch);
  if(!domain&&logic==0&&width<=eps&&limit>=180-eps&&ratio>=99.9999f){float l=lin(threshold);return 1-l/std::max(x,l);}
  float excess=knee(db(x)-threshold,width),factor=logic==2?ratio-1:(ratio>=99.9999f?1.f:1-1/ratio);float gr=std::clamp(excess*factor,0.f,limit);
  return domain?gr:logic==0?1-lin(-gr):lin(gr);
 }
 float filter(int ch,int index){
  int t=7+index*3;float x=target(t,ch);auto& s=states[ch][8+index];
  if(!s.filterReady){double hz=std::pow(2.,target(t+2,ch));hz=std::min(hz,(double)rate*.49);double w=2*M_PI*hz/rate,alpha=std::sin(w)/(2*std::sqrt(.5)),norm=1/(1+alpha);s.b0=(float)((1-std::cos(w))*.5*norm);s.b1=2*s.b0;s.b2=s.b0;s.a1=(float)(-2*std::cos(w)*norm);s.a2=(float)((1-alpha)*norm);double hp=std::pow(2.,target(t+1,ch));s.hpEnabled=target(t+1,ch)>preset.targets[t+1].low+eps;if(s.hpEnabled){hp=std::min(hp,(double)rate*.49);double hw=2*M_PI*hp/rate,ha=std::sin(hw)/(2*std::sqrt(.5)),hn=1/(1+ha);s.hpb0=(float)((1+std::cos(hw))*.5*hn);s.hpb1=-2*s.hpb0;s.hpb2=s.hpb0;s.hpa1=(float)(-2*std::cos(hw)*hn);s.hpa2=(float)((1-ha)*hn);}s.filterReady=true;}
  if(s.hpEnabled){double hy=s.hpb0*x+s.hpz1;s.hpz1=s.hpb1*x-s.hpa1*hy+s.hpz2;s.hpz2=s.hpb2*x-s.hpa2*hy;x=(float)hy;}
  double y=s.b0*x+s.z1;s.z1=s.b1*x-s.a1*y+s.z2;s.z2=s.b2*x-s.a2*y;return (float)y;
 }
 float convolver(int ch,int index){
  int t=127+index*5;auto& s=states[ch][56+index];float x=target(t,ch),ms=target(t+1,ch),asym=target(t+3,ch),power=target(t+4,ch),amount=target(t+2,ch);
  int length=std::clamp((int)((double)ms*.001*(double)rate)+1,1,(int)s.ring.v.size()-256);
  std::array<float,4> settings{(float)length,asym,power,amount};if(settings!=s.kernelSettings){s.kernelSettings=settings;s.kernel.resize(length);double total=0;int shape=convolverShapes[index];for(int j=0;j<length;++j){s.kernel[j]=(float)windowValue(j,length,shape,asym,power,amount);total+=s.kernel[j];}float scale=(float)(1/total);for(float& k:s.kernel)k*=scale;}
  s.ring.write(x);double sum=0;int j=0;
#if defined(__aarch64__)
  float64x2_t a=vdupq_n_f64(0),b=vdupq_n_f64(0);
  for(;j+3<length;j+=4){int index=(s.ring.pos-256-j)&s.ring.mask;float32x4_t samples;
   if(index>=3){samples=vld1q_f32(s.ring.v.data()+index-3);samples=vrev64q_f32(samples);samples=vextq_f32(samples,samples,2);}
   else {float values[]{s.ring.past(256+j),s.ring.past(257+j),s.ring.past(258+j),s.ring.past(259+j)};samples=vld1q_f32(values);}
   float32x4_t weights=vld1q_f32(s.kernel.data()+j);
   a=vaddq_f64(a,vmulq_f64(vcvt_f64_f32(vget_low_f32(samples)),vcvt_f64_f32(vget_low_f32(weights))));
   b=vaddq_f64(b,vmulq_f64(vcvt_f64_f32(vget_high_f32(samples)),vcvt_f64_f32(vget_high_f32(weights))));
  }
  sum=(vgetq_lane_f64(a,0)+vgetq_lane_f64(a,1))+(vgetq_lane_f64(b,0)+vgetq_lane_f64(b,1));
#endif
  for(;j<length;++j)sum+=(double)s.kernel[j]*s.ring.past(256+j);s.ring.next();return (float)sum;
 }
 float average(int ch,int index){
  int t=115+index*2;auto& s=states[ch][50+index];float x=target(t,ch),ms=target(t+1,ch);int window=std::clamp((int)(ms*(rate*.001f))+1,1,(int)s.ring.v.size());
  // Standard causal arithmetic mean of the positive reduction-control stream.
  if(window!=s.window){s.window=window;s.sum=0;for(int j=1;j<window;++j)s.sum+=s.ring.past(j);}
  else s.sum-=s.ring.past(window);s.ring.write(x);s.sum+=x;float y=(float)(s.sum/window);s.ring.next();return y;
 }
 float vca(int ch,int index){
  int t=307+index*3;auto& s=states[ch][102+index];float x=target(t,ch),control=target(t+1,ch);int maxAhead=index<aheadStages?(int)(rate*.005):0;
  int cv=vcaConvolvers[index],clip=vcaClippers[index];
  if(!maxAhead){cv=0;clip=0;}int audioDelay=maxAhead+256*cv+(int)(rate*.05)*clip;
  int requested=std::clamp((int)((double)target(t+2,ch)*.001*rate),0,maxAhead);int gainDelay=maxAhead-requested;
  int domain=vcaDomains[index];float g=domain==1?lin(control):domain==0?1-control:control;
  float own=s.gain.step(g,gainDelay),incoming=1;for(auto route:preset.targets[t].slots)if(route.source>=102&&route.source<108)incoming=netGains[ch][route.source-102];netGains[ch][index]=s.metadata.step(incoming,audioDelay)*own;return s.audio.step(x,audioDelay)*own;
 }
 float source(int id,int ch){
  if(id<0||id>=108)throw std::runtime_error("Source bounds");if(sourceFlags[ch][id]==2)return sourceValues[ch][id];if(sourceFlags[ch][id]==1)throw std::runtime_error("Source cycle "+std::to_string(id));sourceFlags[ch][id]=1;float y=0;
  if(id<2)y=id==0?input[ch]:trigger[ch];else if(id<8)y=target(id-2,ch);else if(id<14)y=filter(ch,id-8);
  else if(id>=32&&id<38)y=detector(ch,id-32);
  else if(id>=38&&id<44){float x=target(97+id-38,ch);int mode=converterModes[id-38];y=mode==0?db(x):mode==1?lin(x):mode==2?-db(1-x):1-lin(-x);}
  else if(id>=44&&id<50){int t=103+(id-44)*2;float x=target(t,ch),ms=target(t+1,ch);y=states[ch][id].maximum.step(x,(int)(ms*(rate*.001f)));}
  else if(id>=50&&id<56)y=average(ch,id-50);
  else if(id>=56&&id<62)y=convolver(ch,id-56);
  else if(id>=62&&id<74)y=capacitor(target(157+(id-62)*8,ch),ch,id-62);
  else if(id>=74&&id<80){int t=253+(id-74)*2;float x=target(t,ch),strength=target(t+1,ch);int mode=transferModes[id-74];if(mode==1)y=std::pow(std::max(0.f,x),1+15*strength);else{float k=(float)((double)(15*strength)+.01);y=std::expm1(x*k)/std::expm1(k);}}
  else if(id>=80&&id<84){int t=265+(id-80)*3;float a=target(t,ch),b=target(t+1,ch),mix=target(t+2,ch);mix=(mix+1)*.5f;y=a*(1-mix)+b*mix;}
  else if(id>=84&&id<90){int t=277+(id-84)*2;float a=target(t,ch),b=target(t+1,ch);int op=operatorModes[id-84];y=op==7?std::max(a,b):op==8?std::min(a,b):op==3?a*b:b!=0?a/b:0;}
  else if(id>=90&&id<96){int t=289+(id-90)*2;float x=target(t,ch);int d=(int)((double)target(t+1,ch)*.001*rate);y=states[ch][id].ring.step(x,std::clamp(d,0,states[ch][id].ring.mask));}
  else if(id>=96&&id<102)y=target(301+id-96,ch);
  else if(id>=102)y=vca(ch,id-102);
  else throw std::runtime_error("Unsupported active source "+std::to_string(id));
  sourceValues[ch][id]=y;sourceFlags[ch][id]=2;return y;
 }
public:
 int latency=0;uint64_t safetyActivations=0;
 Engine(std::string style,float sr,float gain,float attack,float release,float ahead,float out=0):rate(sr),gainDb(gain),attackMs(attack),releaseMs(release),aheadMs(ahead),ceiling(out){
  auto p=std::find_if(presets.begin(),presets.end(),[&](auto& p){return p.name==style;});if(p==presets.end())throw std::runtime_error("Unknown style");preset=*p;aheadStages=parameter("Num Look-ahead Stages");
  for(int i=0;i<6;++i){std::string n=std::to_string(i+1);detectorDomains[i]=parameter("Detector "+n+" Domain");detectorPurposes[i]=parameter("Detector "+n+" Purpose");detectorModes[i]=parameter("Detector "+n+" Mode");vcaDomains[i]=parameter("VCA "+n+" Domain");vcaConvolvers[i]=parameter("VCA "+n+" Num Convolvers In Gain Path");vcaClippers[i]=parameter("VCA "+n+" Num SmartClippers In Gain Path");convolverShapes[i]=parameter("Convolver "+n+" Shape");converterModes[i]=parameter("Converter "+n+" Conversion");transferModes[i]=parameter("Transfer Curve "+n+" Function");operatorModes[i]=parameter("Operator "+n+" Operation",7);}

  latency=(int)(rate*.005)*parameter("Num Look-ahead Stages")+256*parameter("Num Output Convolvers")+(int)(rate*.05)*parameter("Num Output SmartClippers");
  for(auto& ch:states){
   for(int i=44;i<44+parameter("Num Active Peak Hold Sources");++i)ch[i].maximum.prepare((int)(rate*1.1),preset.targets[104+(i-44)*2].globalOnly);
   for(int i=32;i<32+parameter("Num Active Detectors");++i)if(parameter("Detector "+std::to_string(i-31)+" Mode")==2)ch[i].clip.prepare((int)(rate*.05));
   for(auto range:std::array<std::pair<int,int>,3>{{{50,parameter("Num Active AveragingFilter Sources")},{56,parameter("Num Active Convolver Sources")},{90,parameter("Num Active Delay Sources")}}})for(int i=range.first;i<range.first+range.second;++i){ch[i].ring.prepare((int)(rate*1.1)+1024);if(i>=56&&i<62)ch[i].kernel.reserve(ch[i].ring.v.size());}
   for(int i=102;i<102+parameter("Num Active VCAs");++i){ch[i].audio.prepare((int)(rate*.16)+1024);ch[i].metadata.prepare((int)(rate*.16)+1024);std::fill(ch[i].metadata.v.begin(),ch[i].metadata.v.end(),1.f);ch[i].gain.prepare((int)(rate*.02)+1024);std::fill(ch[i].gain.v.begin(),ch[i].gain.v.end(),1.f);}
  }
 }
 std::array<float,2> process(float left,float right){return processTriggered(left,right,left,right);}
 std::array<float,2> processTriggered(float left,float right,float triggerLeft,float triggerRight){input={left,right};trigger={triggerLeft,triggerRight};globalFlags.fill(0);for(auto& f:localFlags)f.fill(0);for(auto& f:sourceFlags)f.fill(0);std::array<float,2> y;for(int c=0;c<2;++c)y[c]=target(6,c)*lin(gainDb+ceiling-.001f);float limit=lin(ceiling),peak=std::max(std::abs(y[0]),std::abs(y[1]));if(peak>limit){++safetyActivations;for(auto& z:y)z*=limit/peak;}++sample;return y;}
 void reset(){sample=0;globalFlags.fill(0);for(auto& f:localFlags)f.fill(0);for(auto& f:sourceFlags)f.fill(0);for(auto& ch:netGains)ch.fill(1);for(auto& ch:states)for(auto& s:ch){
  for(auto* ring:{&s.ring,&s.audio,&s.gain,&s.metadata,&s.maximum.r,&s.clip.ring}){std::fill(ring->v.begin(),ring->v.end(),ring==&s.gain||ring==&s.metadata?1.f:0.f);ring->pos=0;}
  std::fill(s.maximum.tree.begin(),s.maximum.tree.end(),0.f);s.maximum.head=s.maximum.count=0;s.maximum.lastWindow=-1;s.maximum.time=0;s.clip.head=s.clip.size=0;s.clip.previous={0,0};s.clip.count=0;s.clip.prevRaw=s.clip.prevAmount=0;s.clip.prevDirection=0;s.envelope=0;s.lastA=s.lastR=0;s.alphaA=s.alphaR=1;s.sum=0;s.window=0;s.kernelSettings={-1,-1,-1,-1};s.z1=s.z2=s.hpz1=s.hpz2=0;s.filterReady=false;
 }safetyActivations=0;}
 void setLinks(float transient,float release){transientLink=std::clamp(transient,0.f,1.f);releaseLink=std::clamp(release,0.f,1.f);}
 void setParameters(float gain,float attack,float release,float ahead,float out){gainDb=std::clamp(gain,0.f,60.f);attackMs=std::clamp(attack,0.f,10000.f);releaseMs=std::clamp(release,0.f,10000.f);aheadMs=std::clamp(ahead,0.f,5.f);ceiling=std::clamp(out,-30.f,0.f);}
 std::array<float,2> reductionDb()const{std::array<float,2> r{};int source=preset.targets[6].slots.at(0).source-102;for(int ch=0;ch<2;++ch)r[ch]=-db(netGains[ch][source]);return r;}
 float debugTarget(int id,int ch)const{return localFlags[ch][id]==2?localValues[ch][id]:NAN;}
 float debugSource(int id,int ch)const{return sourceFlags[ch][id]==2?sourceValues[ch][id]:NAN;}
};
}
