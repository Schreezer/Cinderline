#include "Sim/Navigation.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace cinder;

namespace {

void check(bool condition,const std::string& message) {
  if(!condition)throw std::runtime_error(message);
}

std::uint32_t bits(float value) {
  std::uint32_t result=0;
  std::memcpy(&result,&value,sizeof(result));
  return result;
}

bool exactPoint(Vec2 a,Vec2 b) {
  return bits(a.x)==bits(b.x)&&bits(a.y)==bits(b.y);
}

void checkSame(const NavigationResult& a,const NavigationResult& b,const std::string& context) {
  check(a.reached==b.reached,context+" changed reached");
  check(a.exhausted==b.exhausted,context+" changed exhausted");
  check(bits(a.cost)==bits(b.cost),context+" changed cost");
  check(a.expanded==b.expanded,context+" changed expanded nodes");
  check(exactPoint(a.origin,b.origin),context+" changed origin");
  check(a.points.size()==b.points.size(),context+" changed path length");
  for(std::size_t index=0;index<a.points.size();++index)
    check(exactPoint(a.points[index],b.points[index]),context+" changed a path point");
}

Navigation sealedStartFixture() {
  Navigation navigation;
  navigation.sync(1000,{
    {{200,300},{10,110}},{{400,300},{10,110}},
    {{300,200},{110,10}},{{300,400},{110,10}},
    {{700,300},{20,120}}
  },{});
  return navigation;
}

void checkPath(const Navigation& navigation,const NavigationResult& route,
               const std::vector<Vec2>& goals,float clearance) {
  check(route.reached,"expected a reached route");
  Vec2 cursor=route.origin;
  float cost=0;
  for(Vec2 point:route.points) {
    check(navigation.segmentClear(cursor,point,clearance),"route segment is not clear from its origin");
    cost+=std::hypot(point.x-cursor.x,point.y-cursor.y);
    cursor=point;
  }
  bool endedAtGoal=false;
  for(Vec2 goal:goals)if(exactPoint(cursor,goal))endedAtGoal=true;
  check(endedAtGoal,"route did not end at a supplied goal");
  check(std::fabs(cost-route.cost)<0.001f,"route cost does not describe the origin-to-goal path");
}

void sealedFirstStartUsesReachableLaterStart() {
  Navigation navigation=sealedStartFixture();
  const Vec2 sealed{300,300},reachable{550,300},goal{850,300};
  check(!navigation.route(sealed,{goal},8).reached,"sealed source unexpectedly reaches the goal");
  check(navigation.route(reachable,{goal},8).reached,"later source fixture is not reachable");
  const auto route=navigation.routeFromAny({sealed,reachable},{goal},8);
  check(route.reached&&!route.exhausted,"multi-source route did not use a reachable source");
  check(exactPoint(route.origin,reachable),"multi-source route reported the sealed source");
  check(route.expanded>0,"fixture unexpectedly bypassed the shared graph search");
  checkPath(navigation,route,{goal},8);
}

void unreachableSourcesReturnCompleteFailure() {
  Navigation navigation=sealedStartFixture();
  const auto route=navigation.routeFromAny({{280,300},{320,300}},{{850,300}},8);
  check(!route.reached&&!route.exhausted,"statically sealed sources did not return complete failure");
  check(route.points.empty()&&route.cost==0,"failed multi-source route retained a path");
}

void tiesAreIndependentOfStartOrder() {
  Navigation navigation;
  navigation.sync(1000,{{{500,504},{30,110}}},{});
  const Vec2 lower{200,400},upper{200,608},goal{800,504};
  check(!navigation.segmentClear(lower,goal,8)&&!navigation.segmentClear(upper,goal,8),
        "tie fixture unexpectedly has a direct route");
  const auto forward=navigation.routeFromAny({lower,upper},{goal},8);
  const auto reverse=navigation.routeFromAny({upper,lower},{goal},8);
  check(forward.reached&&forward.expanded>0,"tie fixture did not use graph routing");
  check(exactPoint(forward.origin,lower),"equal routes did not choose the canonical source");
  checkSame(forward,reverse,"reordered equal sources");
  checkPath(navigation,forward,{goal},8);
}

void adaptivePassAvoidsLargeDeadEndBudget() {
  Navigation navigation;
  navigation.sync(6000,{
    {{4400,3000},{20,2200}},
    {{2700,5200},{1700,20}},
    {{2700,800},{1700,20}}
  },{});
  const Vec2 lower{4200,2944},upper{4200,3056},goal{4700,3000};
  check(!navigation.segmentClear(lower,goal,12)&&!navigation.segmentClear(upper,goal,12),
        "large dead-end fixture unexpectedly has a direct route");
  check(navigation.route(lower,{goal},12).reached&&navigation.route(upper,{goal},12).reached,
        "an individual source cannot leave the large dead end");

  const auto forward=navigation.routeFromAny({lower,upper},{goal},12);
  const auto reverse=navigation.routeFromAny({upper,lower},{goal},12);
  check(forward.reached&&!forward.exhausted,"multi-source adaptive pass exhausted in a reachable basin");
  check(forward.expanded>0&&forward.expanded<40000,"large dead-end route used an invalid expansion count");
  check(exactPoint(forward.origin,lower)||exactPoint(forward.origin,upper),
        "large dead-end route reported an unknown origin");
  checkSame(forward,reverse,"reordered sources in a large dead end");
  checkPath(navigation,forward,{goal},12);
}

Navigation cacheFixture(const std::vector<NavCircle>& circles={}) {
  Navigation navigation;
  navigation.sync(1200,{{{600,600},{35,350}}},circles);
  return navigation;
}

const std::vector<Vec2> CacheGoals{{1040,420},{1040,780},{1040,600}};

void singletonPreservesRouteAndWarmCacheBehavior() {
  const Vec2 start{160,600};
  Navigation navigation=cacheFixture();
  const auto single=navigation.route(start,CacheGoals,12);
  const auto singleton=navigation.routeFromAny({start},CacheGoals,12);
  check(single.reached&&single.expanded>0,"singleton fixture does not exercise graph routing");
  checkSame(single,singleton,"singleton multi-source route");

  Navigation warmed=cacheFixture();
  check(warmed.routeFromAny({{160,600},{180,550}},CacheGoals,12).reached,
        "multi-source cache fixture did not warm");
  const std::vector<Vec2> nextStarts{{170,640},{190,530}};
  Navigation fresh=cacheFixture();
  checkSame(warmed.routeFromAny(nextStarts,CacheGoals,12),
            fresh.routeFromAny(nextStarts,CacheGoals,12),
            "warmed multi-source endpoint route");

  const std::vector<NavCircle> circles{{77,{600,190},70}};
  Navigation ignored=cacheFixture(circles);
  check(ignored.routeFromAny({{160,600},{180,550}},CacheGoals,12).reached,
        "real-ignore fixture did not warm ordinary endpoints");
  Navigation ignoredFresh=cacheFixture(circles);
  checkSame(ignored.routeFromAny(nextStarts,CacheGoals,12,77),
            ignoredFresh.routeFromAny(nextStarts,CacheGoals,12,77),
            "multi-source route with a real ignored obstacle");
}

void geometryChangesAndCopiesStayIsolated() {
  Navigation original;
  original.sync(1000,{},{});
  const std::vector<Vec2> starts{{300,300},{550,300}};
  const std::vector<Vec2> goals{{850,300}};
  const auto open=original.routeFromAny(starts,goals,8);
  check(open.reached&&exactPoint(open.origin,starts[1]),"open fixture chose the wrong source");

  Navigation copy=original;
  original.sync(1000,{
    {{200,300},{10,110}},{{400,300},{10,110}},
    {{300,200},{110,10}},{{300,400},{110,10}},{{700,300},{20,120}}
  },{});
  const auto changed=original.routeFromAny(starts,goals,8);
  check(changed.reached&&exactPoint(changed.origin,starts[1])&&changed.expanded>0,
        "changed geometry did not route from the remaining connected source");
  checkSame(copy.routeFromAny(starts,goals,8),open,"copied source geometry");

  Navigation fresh=sealedStartFixture();
  checkSame(changed,fresh.routeFromAny(starts,goals,8),"multi-source route after geometry sync");
}

void cappedSearchRemainsIndeterminate() {
  Navigation navigation;
  // Ignoring a real, unrelated circle deliberately disables component pruning.
  // The isolated left basin is large enough to reach the shared expansion cap.
  navigation.sync(6000,{{{4800,3000},{20,3000}}},{{77,{400,400},20}});
  const auto route=navigation.routeFromAny({{800,2900},{800,3100}},{{5400,3000}},12,77);
  check(!route.reached&&route.exhausted,"capped search was classified as a complete no-route result");
  check(route.expanded==40000,"adaptive and grid passes did not share the forty-thousand-node cap");
  check(route.points.empty(),"capped search exposed an incomplete path as usable");
}

void invalidStartsAreFilteredDeterministically() {
  Navigation navigation;
  navigation.sync(600,{},{});
  const Vec2 valid{100,100},goal{500,500};
  const float nan=std::numeric_limits<float>::quiet_NaN();
  check(!navigation.routeFromAny({}, {goal},8).reached,"empty starts produced a route");
  check(!navigation.routeFromAny({{nan,100},{100,nan}}, {goal},8).reached,
        "non-finite starts produced a route");
  const auto filtered=navigation.routeFromAny({{nan,100},valid,{100,nan}}, {goal},8);
  const auto ordinary=navigation.route(valid,{goal},8);
  checkSame(filtered,ordinary,"filtered invalid starts");
}

} // namespace

int main() {
  const std::vector<std::pair<std::string,std::function<void()>>> tests{
    {"sealed first start uses reachable later start",sealedFirstStartUsesReachableLaterStart},
    {"unreachable sources return complete failure",unreachableSourcesReturnCompleteFailure},
    {"ties are independent of start order",tiesAreIndependentOfStartOrder},
    {"adaptive pass avoids large dead-end budget",adaptivePassAvoidsLargeDeadEndBudget},
    {"singleton preserves route and warm cache behavior",singletonPreservesRouteAndWarmCacheBehavior},
    {"geometry changes and copies stay isolated",geometryChangesAndCopiesStayIsolated},
    {"capped search remains indeterminate",cappedSearchRemainsIndeterminate},
    {"invalid starts are filtered deterministically",invalidStartsAreFilteredDeterministically},
  };
  int failed=0;
  for(const auto& [name,test]:tests) {
    try {test();std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
  }
  std::cout<<"RESULT passed="<<tests.size()-failed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
