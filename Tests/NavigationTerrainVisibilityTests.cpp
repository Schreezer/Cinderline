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

void checkSame(const NavigationResult& cached,const NavigationResult& fresh,
               const std::string& context) {
  check(cached.reached==fresh.reached,context+" changed reached");
  check(cached.exhausted==fresh.exhausted,context+" changed exhausted");
  check(bits(cached.cost)==bits(fresh.cost),context+" changed cost");
  check(cached.expanded==fresh.expanded,context+" changed expanded nodes");
  check(exactPoint(cached.origin,fresh.origin),context+" changed origin");
  check(cached.points.size()==fresh.points.size(),context+" changed path length");
  for(std::size_t index=0;index<cached.points.size();++index)
    check(exactPoint(cached.points[index],fresh.points[index]),
          context+" changed an exact path point");
}

bool exactStats(const NavigationVisibilityStats& a,const NavigationVisibilityStats& b) {
  return a.hits==b.hits&&a.misses==b.misses&&
         a.saturationMisses==b.saturationMisses&&a.resets==b.resets&&
         a.pages==b.pages&&a.targets==b.targets&&a.payloadBytes==b.payloadBytes;
}

constexpr float World=1200;
const std::vector<NavBox> Wall{{{600,600},{35,350}}};
const Vec2 Start{160,600};
const Vec2 Goal{1040,420};
constexpr float Clearance=12;

Navigation fixture(const std::vector<NavCircle>& circles={}) {
  Navigation navigation;
  navigation.sync(World,Wall,circles);
  return navigation;
}

NavigationResult forcedRoute(Navigation& navigation,Vec2 goal=Goal,float clearance=Clearance) {
  check(!navigation.segmentClear(Start,goal,clearance),
        "visibility fixture unexpectedly uses the direct-goal fast path");
  const auto route=navigation.route(Start,{goal},clearance);
  check(route.reached&&route.expanded>0,"visibility fixture did not use graph routing");
  return route;
}

void circleOnlyChangesReuseTerrainAndRemainExact() {
  Navigation warmed=fixture();
  const auto baseline=forcedRoute(warmed);
  const auto warmStats=warmed.visibilityStats();
  check(warmStats.pages==1&&warmStats.targets>0&&warmStats.misses>0,
        "forced route did not populate one terrain visibility page");
  check(warmStats.payloadBytes==4096,"one visibility page does not use its fixed 4 KiB payload");

  Vec2 cursor=Start;
  check(!baseline.points.empty(),"circle mutation fixture has no routed segment to obstruct");
  const Vec2 firstWaypoint=baseline.points.front();
  const Vec2 blocker{(cursor.x+firstWaypoint.x)*0.5f,(cursor.y+firstWaypoint.y)*0.5f};
  const std::vector<NavCircle> added{{71,blocker,18}};
  warmed.sync(World,Wall,added);
  const auto afterAddSync=warmed.visibilityStats();
  check(afterAddSync.resets==warmStats.resets&&afterAddSync.pages==warmStats.pages,
        "circle addition cleared terrain-only visibility state");
  check(!warmed.segmentClear(cursor,firstWaypoint,Clearance),
        "added circle does not obstruct a segment from the previously chosen route");
  Navigation addFresh=fixture(added);
  const auto addedRoute=forcedRoute(warmed);
  checkSame(addedRoute,forcedRoute(addFresh),"route after circle addition");
  const auto afterAddRoute=warmed.visibilityStats();
  check(afterAddRoute.hits>warmStats.hits,
        "circle addition did not reuse any stable terrain visibility result");

  check(!addedRoute.points.empty(),"circle replacement fixture has no routed segment to obstruct");
  const Vec2 replacementWaypoint=addedRoute.points.front();
  const Vec2 replacementBlocker{(Start.x+replacementWaypoint.x)*0.5f,
                                (Start.y+replacementWaypoint.y)*0.5f};
  const std::vector<NavCircle> replaced{{71,replacementBlocker,18}};
  warmed.sync(World,Wall,replaced);
  const auto afterReplaceSync=warmed.visibilityStats();
  check(afterReplaceSync.resets==warmStats.resets&&afterReplaceSync.pages==1,
        "circle replacement cleared terrain-only visibility state");
  check(!warmed.segmentClear(Start,replacementWaypoint,Clearance),
        "replacement circle does not obstruct the newly chosen route segment");
  Navigation replaceFresh=fixture(replaced);
  checkSame(forcedRoute(warmed),forcedRoute(replaceFresh),"route after circle replacement");

  warmed.sync(World,Wall,{});
  const auto afterRemoveSync=warmed.visibilityStats();
  check(afterRemoveSync.resets==warmStats.resets&&afterRemoveSync.pages==1,
        "circle removal cleared terrain-only visibility state");
  check(warmed.segmentClear(Start,replacementWaypoint,Clearance),
        "removed circle still obstructs its former route segment");
  Navigation removeFresh=fixture();
  checkSame(forcedRoute(warmed),forcedRoute(removeFresh),"route after circle removal");
}

void terrainChangesResetButNormalizedReorderPreserves() {
  const std::vector<NavBox> boxes{
    {{600,600},{35,350}},{{300,100},{40,20}},{{900,1100},{30,15}}
  };
  const std::vector<NavCircle> circles{{8,{260,930},18},{3,{940,250},22}};
  Navigation navigation;
  navigation.sync(World,boxes,circles);
  check(navigation.route(Start,{Goal},Clearance).reached,"reorder fixture did not warm");
  const auto version=navigation.geometryVersion();
  const auto warmed=navigation.visibilityStats();
  check(warmed.pages==1&&warmed.targets>0,"reorder fixture did not populate visibility state");

  navigation.sync(World,{boxes[2],boxes[0],boxes[1]},{circles[1],circles[0]});
  check(navigation.geometryVersion()==version,"normalized reorder changed geometry version");
  check(exactStats(navigation.visibilityStats(),warmed),
        "normalized reorder changed derived visibility state");

  const float changedWorld=std::nextafter(World,std::numeric_limits<float>::infinity());
  navigation.sync(changedWorld,boxes,circles);
  const auto worldChanged=navigation.visibilityStats();
  check(worldChanged.resets==warmed.resets+1&&worldChanged.pages==0&&
        worldChanged.targets==0&&worldChanged.payloadBytes==0,
        "world-size bit change did not clear terrain visibility state");

  check(navigation.route(Start,{Goal},Clearance).reached,"world-change fixture did not rewarm");
  const auto rewarmed=navigation.visibilityStats();
  auto changedBoxes=boxes;
  changedBoxes[0].half.x=std::nextafter(changedBoxes[0].half.x,
                                      std::numeric_limits<float>::infinity());
  navigation.sync(changedWorld,changedBoxes,circles);
  const auto boxChanged=navigation.visibilityStats();
  check(boxChanged.resets==rewarmed.resets+1&&boxChanged.pages==0&&
        boxChanged.targets==0&&boxChanged.payloadBytes==0,
        "box bit change did not clear terrain visibility state");
}

void clearancesUseDistinctPages() {
  Navigation navigation=fixture();
  forcedRoute(navigation,Goal,8);
  const auto narrow=navigation.visibilityStats();
  check(narrow.pages==1,"first clearance did not create one visibility page");
  Navigation narrowFresh=fixture();
  checkSame(navigation.route(Start,{Goal},8),narrowFresh.route(Start,{Goal},8),
            "warmed narrow-clearance route");

  Navigation wideFresh=fixture();
  const auto wide=forcedRoute(navigation,Goal,20);
  checkSame(wide,forcedRoute(wideFresh,Goal,20),"route at distinct clearance");
  const auto distinct=navigation.visibilityStats();
  check(distinct.pages==2&&distinct.payloadBytes==8192,
        "distinct clearance did not receive a distinct 4 KiB visibility page");
}

void copiesAssignmentsAndMovesKeepVisibilityOwnershipValid() {
  Navigation original=fixture();
  forcedRoute(original);
  const auto originalStats=original.visibilityStats();

  Navigation copy=original;
  check(copy.visibilityStats().pages==0&&copy.visibilityStats().targets==0,
        "copy constructor shared mutable visibility cache ownership");
  Navigation fresh=fixture();
  checkSame(forcedRoute(copy),forcedRoute(fresh),"copied navigation route");
  check(exactStats(original.visibilityStats(),originalStats),
        "routing a copy mutated the source visibility cache");

  Navigation assigned=fixture({{99,{1000,1000},10}});
  forcedRoute(assigned);
  assigned=original;
  check(assigned.visibilityStats().pages==0&&assigned.visibilityStats().targets==0,
        "copy assignment retained or shared mutable visibility state");
  Navigation assignedFresh=fixture();
  checkSame(forcedRoute(assigned),forcedRoute(assignedFresh),"copy-assigned navigation route");

  const auto copyStats=copy.visibilityStats();
  Navigation moved=std::move(copy);
  check(exactStats(moved.visibilityStats(),copyStats),
        "move construction lost visibility cache state");
  Navigation movedFresh=fixture();
  checkSame(moved.route(Start,{Goal},Clearance),movedFresh.route(Start,{Goal},Clearance),
            "move-constructed navigation route");

  const auto assignedStats=assigned.visibilityStats();
  Navigation moveAssigned;
  moveAssigned=std::move(assigned);
  check(exactStats(moveAssigned.visibilityStats(),assignedStats),
        "move assignment lost visibility cache state");
  Navigation moveAssignedFresh=fixture();
  checkSame(moveAssigned.route(Start,{Goal},Clearance),
            moveAssignedFresh.route(Start,{Goal},Clearance),
            "move-assigned navigation route");
}

void fifoPageBudgetRetainsExactRoutes() {
  constexpr std::size_t PageBudget=1024;
  Navigation navigation;
  const std::vector<NavBox> boxes{{{192,192},{20,120}}};
  navigation.sync(384,boxes,{});
  const Vec2 start{48,192};
  std::vector<Vec2> goals;
  goals.reserve(PageBudget+1);
  for(std::size_t index=0;index<=PageBudget;++index) {
    const Vec2 goal{336,100+static_cast<float>(index)*0.08f};
    check(!navigation.segmentClear(start,goal,6),
          "FIFO fixture goal unexpectedly bypasses graph routing");
    const auto route=navigation.route(start,{goal},6);
    check(route.reached&&route.expanded>0,"FIFO fixture goal did not use graph routing");
    goals.push_back(goal);
  }
  const auto full=navigation.visibilityStats();
  check(full.pages==PageBudget&&full.payloadBytes==PageBudget*4096,
        "visibility FIFO exceeded or underfilled its 1024-page payload budget");
  check(full.targets>0&&full.targets<=16384,"visibility target registry exceeded its bound");

  const std::vector<NavCircle> circles{{501,{50,350},2}};
  navigation.sync(384,boxes,circles);
  check(navigation.visibilityStats().pages==PageBudget,
        "circle-only change discarded the full terrain page budget");

  Navigation recentFresh;
  recentFresh.sync(384,boxes,circles);
  const auto beforeRecent=navigation.visibilityStats();
  const auto recent=navigation.route(start,{goals.back()},6);
  checkSame(recent,recentFresh.route(start,{goals.back()},6),"retained newest FIFO page");
  const auto afterRecent=navigation.visibilityStats();
  check(afterRecent.hits>beforeRecent.hits,
        "newest FIFO page did not retain stable terrain visibility results");
  const auto retainedMisses=afterRecent.misses-beforeRecent.misses;

  Navigation oldestFresh;
  oldestFresh.sync(384,boxes,circles);
  const auto oldest=navigation.route(start,{goals.front()},6);
  checkSame(oldest,oldestFresh.route(start,{goals.front()},6),"resumed evicted FIFO goal");
  const auto afterOldest=navigation.visibilityStats();
  check(afterOldest.pages==PageBudget&&afterOldest.payloadBytes==PageBudget*4096,
        "borrowing a page for the oldest goal broke the FIFO budget");
  check(afterOldest.misses-afterRecent.misses>retainedMisses,
        "oldest FIFO goal did not borrow a cold replacement page");
}

void saturatedTargetRegistryResetsAtNextPageBorrow() {
  constexpr std::size_t TargetBudget=16384;
  constexpr int MaxShifts=1600;
  const std::vector<NavBox> boxes{{{96,96},{6,60}}};
  const Vec2 source{24,96},firstGoal{168,72},secondGoal{168,120};
  Navigation navigation;
  NavigationVisibilityStats saturated;
  NavigationResult saturatedRoute;
  int shifts=0;
  for(;shifts<MaxShifts;++shifts) {
    const std::vector<NavCircle> circles{{700,{50+static_cast<float>(shifts)*0.003f,35},6}};
    navigation.sync(192,boxes,circles);
    check(!navigation.segmentClear(source,firstGoal,2),
          "registry fixture unexpectedly bypasses graph routing");
    saturatedRoute=navigation.route(source,{firstGoal},2);
    check(saturatedRoute.reached&&saturatedRoute.expanded>0,
          "registry fixture did not complete its forced graph route");
    saturated=navigation.visibilityStats();
    check(saturated.pages<=1024&&saturated.targets<=TargetBudget&&
          saturated.payloadBytes<=1024*4096,
          "registry fixture exceeded a documented visibility bound");
    if(saturated.saturationMisses>0)break;
  }
  check(shifts<MaxShifts,
        "fixed 1600-shift fixture did not saturate the exact-target registry");
  check(saturated.targets==TargetBudget&&saturated.saturationMisses>0,
        "target registry did not report its bounded saturation miss");

  const std::vector<NavCircle> circles{{700,{50+static_cast<float>(shifts)*0.003f,35},6}};
  Navigation saturatedFresh;
  saturatedFresh.sync(192,boxes,circles);
  checkSame(saturatedRoute,saturatedFresh.route(source,{firstGoal},2),
            "route at target-registry saturation");

  const auto resetRoute=navigation.route(source,{secondGoal},2);
  Navigation resetFresh;
  resetFresh.sync(192,boxes,circles);
  checkSame(resetRoute,resetFresh.route(source,{secondGoal},2),
            "route after deferred registry reset and reindex");
  const auto reset=navigation.visibilityStats();
  check(reset.resets==saturated.resets+1,
        "pending registry saturation did not reset at the next page borrow");
  check(reset.pages==1&&reset.targets>0&&reset.targets<TargetBudget&&
        reset.payloadBytes==4096,
        "post-saturation page and target registry were not rebuilt within bounds");
  std::cout<<"VISIBILITY_SATURATION shifts="<<shifts+1<<" saturation_misses="
           <<reset.saturationMisses<<" resets="<<reset.resets<<" targets="<<reset.targets<<'\n';
}

} // namespace

int main() {
  const std::vector<std::pair<std::string,std::function<void()>>> tests{
    {"circle-only changes reuse terrain and remain exact",circleOnlyChangesReuseTerrainAndRemainExact},
    {"terrain changes reset but normalized reorder preserves",terrainChangesResetButNormalizedReorderPreserves},
    {"clearances use distinct pages",clearancesUseDistinctPages},
    {"copies assignments and moves keep visibility ownership valid",copiesAssignmentsAndMovesKeepVisibilityOwnershipValid},
    {"FIFO page budget retains exact routes",fifoPageBudgetRetainsExactRoutes},
    {"saturated target registry resets at next page borrow",saturatedTargetRegistryResetsAtNextPageBorrow},
  };
  int failed=0;
  for(const auto& [name,test]:tests)try {
    test();std::cout<<"PASS "<<name<<'\n';
  } catch(const std::exception& error) {
    ++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';
  }
  std::cout<<"RESULT passed="<<tests.size()-failed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
