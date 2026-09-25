#include "Sim/Navigation.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
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

void checkSame(const NavigationResult& cached,const NavigationResult& fresh,
               const std::string& context) {
  check(cached.reached==fresh.reached,context+" changed reached");
  check(cached.exhausted==fresh.exhausted,context+" changed exhausted");
  check(bits(cached.cost)==bits(fresh.cost),context+" changed cost");
  check(cached.expanded==fresh.expanded,context+" changed expanded nodes");
  check(cached.points.size()==fresh.points.size(),context+" changed path length");
  for(std::size_t index=0;index<cached.points.size();++index)
    check(bits(cached.points[index].x)==bits(fresh.points[index].x)&&
          bits(cached.points[index].y)==bits(fresh.points[index].y),
          context+" changed an exact path point");
}

Navigation navigationWith(const std::vector<NavCircle>& circles={}) {
  Navigation navigation;
  navigation.sync(1200,{{{600,600},{35,350}}},circles);
  return navigation;
}

const std::vector<Vec2> Goals{{1040,420},{1040,780},{1040,600}};

void warmCachePreservesExactRoute() {
  Navigation warmed=navigationWith();
  Navigation fresh=navigationWith();
  const Vec2 firstStart{160,600};
  for(Vec2 goal:Goals)
    check(!warmed.segmentClear(firstStart,goal,12),
          "route fixture unexpectedly uses the direct-goal fast path");
  const auto first=warmed.route(firstStart,Goals,12);
  check(first.reached&&first.expanded>0,"route fixture does not exercise graph attachments");

  const Vec2 secondStart{180,550};
  const auto cached=warmed.route(secondStart,Goals,12);
  const auto uncached=fresh.route(secondStart,Goals,12);
  checkSame(cached,uncached,"warmed endpoint route");
}

void cachedEndpointsRestoreCurrentGoalIndices() {
  Navigation warmed=navigationWith();
  check(warmed.route({160,600},Goals,12).reached,"goal-index fixture does not warm");

  const std::vector<Vec2> reordered{Goals[2],Goals[0]};
  Navigation reorderedFresh=navigationWith();
  checkSame(warmed.route({170,640},reordered,12),
            reorderedFresh.route({170,640},reordered,12),
            "reordered cached goals");

  const std::vector<Vec2> filtered{{600,600},Goals[2],Goals[2]};
  Navigation filteredFresh=navigationWith();
  const auto cached=warmed.route({175,590},filtered,12);
  const auto fresh=filteredFresh.route({175,590},filtered,12);
  check(cached.reached,"filtered cached goal is not reachable");
  checkSame(cached,fresh,"filtered cached goals");
}

void geometryAndCopiesKeepCachesIsolated() {
  Navigation original=navigationWith();
  check(original.route({160,600},Goals,12).reached,"copy fixture does not warm");
  const auto originalVersion=original.geometryVersion();

  Navigation copy=original;
  check(copy.route({180,550},Goals,12).reached,"copied navigation does not warm its own cache");
  copy.addCircle({91,{600,190},70});
  check(copy.geometryVersion()!=originalVersion,"copy geometry did not detach");
  check(original.geometryVersion()==originalVersion,"copy geometry changed its source");
  Navigation changedFresh=navigationWith({{91,{600,190},70}});
  checkSame(copy.route({180,550},Goals,12),changedFresh.route({180,550},Goals,12),
            "copied navigation after addCircle");

  Navigation originalFresh=navigationWith();
  checkSame(original.route({180,550},Goals,12),originalFresh.route({180,550},Goals,12),
            "source navigation after copied geometry changed");

  original.sync(1200,{{{600,600},{35,350}}},{{92,{600,1010},55}});
  Navigation syncedFresh=navigationWith({{92,{600,1010},55}});
  checkSame(original.route({180,550},Goals,12),syncedFresh.route({180,550},Goals,12),
            "warmed navigation after geometry sync");
}

void clearanceAndRealIgnoreStayDistinct() {
  Navigation warmed=navigationWith();
  check(warmed.route({160,600},Goals,12).reached,"clearance fixture does not warm");
  Navigation wideFresh=navigationWith();
  checkSame(warmed.route({180,550},Goals,28),wideFresh.route({180,550},Goals,28),
            "cached endpoints at another clearance");

  const std::vector<NavCircle> circles{{77,{600,190},70}};
  Navigation ignored=navigationWith(circles);
  check(ignored.route({160,600},Goals,12).reached,"real-ignore fixture does not warm ordinary goals");
  Navigation ignoredFresh=navigationWith(circles);
  const auto cached=ignored.route({180,550},Goals,12,77);
  const auto fresh=ignoredFresh.route({180,550},Goals,12,77);
  check(cached.reached&&cached.expanded>0,"real ignored obstacle route does not use attachments");
  checkSame(cached,fresh,"route with a real ignored obstacle");
}

void boundedUniqueGoalChurnRemainsStable() {
  Navigation churned;
  churned.sync(384,{{{192,192},{20,120}}},{});
  const Vec2 start{48,192};
  const Vec2 firstGoal{336,100};
  check(churned.route(start,{firstGoal},6).reached,"cache-churn fixture does not route");
  for(int index=1;index<2064;++index) {
    const Vec2 goal{336,100+static_cast<float>(index)*0.08f};
    const auto route=churned.route(start,{goal},6);
    check(route.reached,"unique cache-churn goal is not reachable");
  }
  Navigation fresh;
  fresh.sync(384,{{{192,192},{20,120}}},{});
  checkSame(churned.route({52,188},{firstGoal},6),fresh.route({52,188},{firstGoal},6),
            "route after bounded unique-goal churn");
}

} // namespace

int main() {
  const std::vector<std::pair<std::string,std::function<void()>>> tests{
    {"warm cache preserves exact route",warmCachePreservesExactRoute},
    {"cached endpoints restore current goal indices",cachedEndpointsRestoreCurrentGoalIndices},
    {"geometry and copies keep caches isolated",geometryAndCopiesKeepCachesIsolated},
    {"clearance and real ignore stay distinct",clearanceAndRealIgnoreStayDistinct},
    {"bounded unique-goal churn remains stable",boundedUniqueGoalChurnRemainsStable},
  };
  int failed=0;
  for(const auto& [name,test]:tests) {
    try {test();std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
  }
  std::cout<<"RESULT passed="<<tests.size()-failed<<" failed="<<failed<<'\n';
  return failed?1:0;
}
