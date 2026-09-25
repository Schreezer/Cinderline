#include "Sim/Simulation.h"
#include <cmath>
#include <iostream>
#include <vector>
using namespace cinder;

static Navigation navigationFor(const Simulation& simulation) {
    std::vector<NavBox> boxes;
    for (const auto& o : simulation.obstacles()) boxes.push_back({o.center, o.half});
    std::vector<NavCircle> circles;
    for (const auto& e : simulation.entities()) if (e.alive() &&
        (definition(e.kind).building || (e.kind == Kind::Resource && e.resource > 0)))
        circles.push_back({e.id, e.pos, definition(e.kind).radius});
    Navigation n; n.sync(simulation.worldSize(), boxes, circles); return n;
}

static Navigation navigationWithout(const Simulation& simulation, Id removed) {
    std::vector<NavBox> boxes;
    for (const auto& o : simulation.obstacles()) boxes.push_back({o.center, o.half});
    std::vector<NavCircle> circles;
    for (const auto& e : simulation.entities()) if (e.id != removed && e.alive() &&
        (definition(e.kind).building || (e.kind == Kind::Resource && e.resource > 0)))
        circles.push_back({e.id, e.pos, definition(e.kind).radius});
    Navigation n; n.sync(simulation.worldSize(), boxes, circles); return n;
}

static void show(const char* label, const Navigation& n, Vec2 from, Vec2 goal, float c, Id ignore) {
    auto r=n.route(from,{goal},c,ignore);
    std::cout<<label<<" c="<<c<<" point="<<n.pointClear(from,c,ignore)
      <<" direct="<<n.segmentClear(from,goal,c,ignore)<<" reached="<<r.reached
      <<" exhausted="<<r.exhausted<<" points="<<r.points.size()<<" cost="<<r.cost
      <<" expanded="<<r.expanded;
    if(!r.points.empty()) std::cout<<" endpoint="<<r.points.back().x<<","<<r.points.back().y;
    std::cout<<"\n";
}

int main(int argc,char**argv) {
    for(int a=1;a<argc;++a) {
      Simulation s; if(!s.load(argv[a])) {std::cerr<<"load failed\n";return 2;}
      auto n=navigationFor(s); std::cout<<"FILE "<<argv[a]<<" tick="<<s.tick()<<" geom="<<n.geometryVersion()<<"\n";
      for(const auto& e:s.entities()) if(e.alive() && (definition(e.kind).building || (e.kind==Kind::Resource && e.resource>0)) && e.pos.x<900 && e.pos.y>3800)
        std::cout<<"blocker id="<<e.id<<" kind="<<definition(e.kind).name<<" center="<<e.pos.x<<","<<e.pos.y<<" radius="<<definition(e.kind).radius<<"\n";
      for(const auto& b:s.obstacles()) if(b.center.x-b.half.x<900 && b.center.y+b.half.y>3800)
        std::cout<<"box center="<<b.center.x<<","<<b.center.y<<" half="<<b.half.x<<","<<b.half.y<<"\n";
      for(Id id:{248u,264u}) {const Entity* e=s.find(id); if(!e) continue;
        std::cout<<"id="<<id<<" pos="<<e->pos.x<<","<<e->pos.y<<" goal="<<e->goal.x<<","<<e->goal.y
          <<" order="<<(int)e->order<<" navex="<<e->navigationExhausted<<" pathGeom="<<e->pathGeometry<<"\n";
        for(float c:{0.f,15.f,20.f,24.f,25.f,26.f,27.f,28.f,29.f,30.f,31.f,34.f}) show("goal",n,e->pos,e->goal,c,id);
        for(Vec2 g:std::vector<Vec2>{{600,3800},{800,4200},{600,4700},{1000,4000},{1500,3250}}) show("local",n,e->pos,g,30,id);
        for(int deg=0;deg<360;deg+=15) {float q=deg*3.14159265358979323846f/180.f; Vec2 g{e->pos.x+180*std::cos(q),e->pos.y+180*std::sin(q)}; auto r=n.route(e->pos,{g},30,id); if(r.reached) std::cout<<"escape deg="<<deg<<" points="<<r.points.size()<<" cost="<<r.cost<<"\n";}
        for(Id removed:{80u,118u,126u,172u}) {auto nr=navigationWithout(s,removed); auto rr=nr.route(e->pos,{e->goal},30,id); std::cout<<"remove="<<removed<<" reached="<<rr.reached<<" exhausted="<<rr.exhausted<<" expanded="<<rr.expanded<<" cost="<<rr.cost<<"\n";}
      }
    }
}
