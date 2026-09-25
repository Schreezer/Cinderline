#include "Sim/Navigation.h"
#include <cmath>
#include <iostream>
#include <vector>
using namespace cinder;
constexpr float Pi=3.14159265358979323846f;
struct Result { int clear=0; bool firstReach=false; bool laterReach=false; bool firstOre=false; bool laterOre=false; Vec2 first{}; };
Result probe(bool ore) {
  const float world=4800, wr=16, pr=125;
  const Vec2 producer{196.949f,4603.05f}, rally{1120,3680};
  const float angle=std::atan2(rally.y-producer.y,rally.x-producer.x);
  const float extent=pr+wr+28;
  const Vec2 first{producer.x+std::cos(angle)*extent,producer.y+std::sin(angle)*extent};
  std::vector<NavBox> boxes={
    {{first.x-23,first.y},{3,26}},{{first.x+23,first.y},{3,26}},
    {{first.x,first.y-23},{26,3}},{{first.x,first.y+23},{26,3}}
  };
  std::vector<NavCircle> circles={
    {1,{600,4200},125},{2,{4200,600},125},
    {3,{387.868f,4412.13f},76},{4,{431.619f,4606.51f},76},
    {5,{193.493f,4368.38f},76},{6,producer,125}
  };
  if(ore) circles.push_back({7,rally,45});
  Navigation nav; nav.sync(world,boxes,circles);
  std::vector<Vec2> goals;
  if(!ore) goals.push_back(rally);
  else {
    const float reach=45+wr+9;
    for(int s=0;s<32;++s) {
      float a=s*2*Pi/32;
      Vec2 p{rally.x+std::cos(a)*reach,rally.y+std::sin(a)*reach};
      if(nav.pointClear(p,wr+.25f)&&nav.segmentClear(p,rally,wr,7))goals.push_back(p);
    }
  }
  Result out; out.first=first;
  std::vector<Vec2> clear;
  for(int ring=0;ring<6;++ring)for(int spoke=0;spoke<16;++spoke) {
    float a=angle+spoke*Pi/8;
    float e=pr+wr+28+ring*35;
    Vec2 p{producer.x+std::cos(a)*e,producer.y+std::sin(a)*e};
    if(p.x<wr||p.y<wr||p.x>world-wr||p.y>world-wr)continue;
    if(nav.pointClear(p,wr,6))clear.push_back(p);
  }
  out.clear=(int)clear.size();
  if(!clear.empty()) {
    auto rr=nav.route(clear.front(),goals,wr); out.firstReach=rr.reached;
    out.firstOre=ore?rr.reached:false;
    for(size_t i=1;i<clear.size();++i) if(nav.route(clear[i],goals,wr).reached) {out.laterReach=true;out.laterOre=ore;break;}
    std::cout<<"first_clear "<<clear.front().x<<" "<<clear.front().y<<" raw "<<first.x<<" "<<first.y<<"\n";
  }
  std::cout<<"goals "<<goals.size()<<" clear "<<out.clear<<" first_reach "<<out.firstReach<<" later_reach "<<out.laterReach<<" center_clear "<<nav.pointClear(first,wr,6)<<"\n";
  return out;
}
int main(){auto a=probe(false);auto b=probe(true);return a.clear>1&&!a.firstReach&&a.laterReach&&b.clear>1&&!b.firstReach&&b.laterReach?0:1;}
