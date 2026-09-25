#include "Sim/Simulation.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using namespace cinder;

namespace {

float distance(Vec2 a,Vec2 b) { return std::hypot(a.x-b.x,a.y-b.y); }
Vec2 subtract(Vec2 a,Vec2 b) { return {a.x-b.x,a.y-b.y}; }
float dot(Vec2 a,Vec2 b) { return a.x*b.x+a.y*b.y; }
Vec2 normalized(Vec2 value) {
  const float length=std::hypot(value.x,value.y);
  return length>0.0001f?Vec2{value.x/length,value.y/length}:Vec2{};
}
float segmentDistanceSquared(Vec2 from,Vec2 to,Vec2 point) {
  const Vec2 delta=subtract(to,from);
  const float lengthSquared=dot(delta,delta);
  const float projection=lengthSquared>0
      ? std::clamp(dot(subtract(point,from),delta)/lengthSquared,0.0f,1.0f):0.0f;
  const Vec2 closest{from.x+delta.x*projection,from.y+delta.y*projection};
  return dot(subtract(point,closest),subtract(point,closest));
}

void report(const Simulation& simulation,Id id,int elapsed,Vec2 low,Vec2 high,
            float minimumGoalDistance,float maximumStep) {
  const Entity* mover=simulation.find(id);
  if(!mover) { std::cout<<"elapsed="<<elapsed<<" missing="<<id<<'\n';return; }
  const Vec2 waypoint=mover->pathIndex>=0&&mover->pathIndex<static_cast<int>(mover->path.size())
      ? mover->path[mover->pathIndex]:mover->goal;
  const Vec2 direction=normalized(subtract(waypoint,mover->pos));
  const Vec2 perpendicular{-direction.y,direction.x};
  std::cout<<"elapsed="<<elapsed<<" id="<<id<<" pos="<<mover->pos.x<<','<<mover->pos.y
           <<" goal="<<mover->goal.x<<','<<mover->goal.y
           <<" goal_distance="<<distance(mover->pos,mover->goal)
           <<" minimum_goal_distance="<<minimumGoalDistance
           <<" envelope="<<low.x<<','<<low.y<<':'<<high.x<<','<<high.y
           <<" maximum_step="<<maximumStep
           <<" order="<<static_cast<int>(mover->order)
           <<" path="<<mover->pathIndex<<'/'<<mover->path.size()
           <<" waypoint="<<waypoint.x<<','<<waypoint.y
           <<" stalled="<<mover->stalledFor<<" failures="<<mover->navigationFailures
           <<" side="<<mover->avoidanceSide<<" yield="<<mover->yieldFor
           <<" exhausted="<<mover->navigationExhausted<<'\n';

  struct Nearby { float gap;const Entity* entity;float forward,lateral,swept; };
  std::vector<Nearby> mobiles,statics;
  const float moverRadius=definition(mover->kind).radius;
  for(const Entity& other:simulation.entities()) {
    if(other.id==id||!other.alive())continue;
    const float gap=distance(mover->pos,other.pos)-moverRadius-definition(other.kind).radius;
    const Vec2 offset=subtract(other.pos,mover->pos);
    Nearby item{gap,&other,dot(offset,direction),dot(offset,perpendicular),
                std::sqrt(segmentDistanceSquared(mover->pos,waypoint,other.pos))-
                  moverRadius-definition(other.kind).radius};
    if(definition(other.kind).building||other.kind==Kind::Resource)statics.push_back(item);
    else if(distance(mover->pos,other.pos)<=180)mobiles.push_back(item);
  }
  auto byGap=[](const Nearby& a,const Nearby& b){return a.gap<b.gap;};
  std::sort(mobiles.begin(),mobiles.end(),byGap);std::sort(statics.begin(),statics.end(),byGap);
  for(const Nearby& item:mobiles) {
    const Entity& other=*item.entity;
    std::cout<<" mobile id="<<other.id<<" kind="<<static_cast<int>(other.kind)
             <<" team="<<other.team<<" pos="<<other.pos.x<<','<<other.pos.y
             <<" gap="<<item.gap<<" forward="<<item.forward<<" lateral="<<item.lateral
             <<" route_gap="<<item.swept<<" order="<<static_cast<int>(other.order)
             <<" own_goal_distance="<<distance(other.pos,other.goal)
             <<" failures="<<other.navigationFailures<<" yield="<<other.yieldFor<<'\n';
  }
  for(std::size_t index=0;index<std::min<std::size_t>(statics.size(),8);++index) {
    const Nearby& item=statics[index];const Entity& other=*item.entity;
    std::cout<<" static id="<<other.id<<" kind="<<static_cast<int>(other.kind)
             <<" team="<<other.team<<" pos="<<other.pos.x<<','<<other.pos.y
             <<" gap="<<item.gap<<" route_gap="<<item.swept<<'\n';
  }
  for(const Obstacle& obstacle:simulation.obstacles()) {
    const float dx=std::max(std::fabs(mover->pos.x-obstacle.center.x)-obstacle.half.x,0.0f);
    const float dy=std::max(std::fabs(mover->pos.y-obstacle.center.y)-obstacle.half.y,0.0f);
    const float gap=std::hypot(dx,dy)-moverRadius;
    if(gap<250)std::cout<<" box center="<<obstacle.center.x<<','<<obstacle.center.y
                         <<" half="<<obstacle.half.x<<','<<obstacle.half.y
                         <<" gap="<<gap<<'\n';
  }

  Navigation navigation;
  std::vector<NavBox> boxes;for(const Obstacle& obstacle:simulation.obstacles())
    boxes.push_back({obstacle.center,obstacle.half});
  std::vector<NavCircle> circles;for(const Entity& other:simulation.entities()) {
    if(!other.alive())continue;
    if(definition(other.kind).building||(other.kind==Kind::Resource&&other.resource>0))
      circles.push_back({other.id,other.pos,definition(other.kind).radius});
  }
  navigation.sync(simulation.worldSize(),boxes,circles);
  const NavigationResult fresh=navigation.route(mover->pos,{mover->goal},moverRadius,mover->id);
  std::cout<<" waypoint_clear="<<navigation.segmentClear(mover->pos,waypoint,moverRadius,mover->id)
           <<" static_direct="<<navigation.segmentClear(mover->pos,mover->goal,moverRadius,mover->id)
           <<" fresh_reached="<<fresh.reached<<" fresh_exhausted="<<fresh.exhausted
           <<" fresh_cost="<<fresh.cost<<" fresh_expanded="<<fresh.expanded
           <<" fresh_points="<<fresh.points.size()<<'\n';
}

} // namespace

int main(int argc,char** argv) {
  if(argc!=2) { std::cerr<<"usage: mortar_stall_current_diagnostic SAVE\n";return 2; }
  Simulation simulation;if(!simulation.load(argv[1])) { std::cerr<<"load failed\n";return 2; }
  constexpr Id Mortar=262;
  Vec2 low=simulation.find(Mortar)->pos,high=low,previous=low;
  float minimumGoalDistance=distance(previous,simulation.find(Mortar)->goal),maximumStep=0;
  for(int elapsed=1;elapsed<=2400;++elapsed) {
    simulation.update(Simulation::Step);
    const Entity* mover=simulation.find(Mortar);if(!mover)break;
    low.x=std::min(low.x,mover->pos.x);low.y=std::min(low.y,mover->pos.y);
    high.x=std::max(high.x,mover->pos.x);high.y=std::max(high.y,mover->pos.y);
    minimumGoalDistance=std::min(minimumGoalDistance,distance(mover->pos,mover->goal));
    maximumStep=std::max(maximumStep,distance(previous,mover->pos));previous=mover->pos;
    if(elapsed%100==0)
      report(simulation,Mortar,elapsed,low,high,minimumGoalDistance,maximumStep);
  }
  return 0;
}
