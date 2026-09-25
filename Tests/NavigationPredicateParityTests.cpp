#include "Sim/Navigation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
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

bool finite(Vec2 point) {
    return std::isfinite(point.x)&&std::isfinite(point.y);
}

float distanceSquared(Vec2 a,Vec2 b) {
    const float x=a.x-b.x,y=a.y-b.y;
    return x*x+y*y;
}

float pointSegmentDistanceSquared(Vec2 point,Vec2 a,Vec2 b) {
    const Vec2 ab{b.x-a.x,b.y-a.y};
    const float lengthSquared=ab.x*ab.x+ab.y*ab.y;
    if(lengthSquared<=1.0e-12f)return distanceSquared(point,a);
    const Vec2 ap{point.x-a.x,point.y-a.y};
    const float t=std::clamp((ap.x*ab.x+ap.y*ab.y)/lengthSquared,0.0f,1.0f);
    return distanceSquared(point,{a.x+ab.x*t,a.y+ab.y*t});
}

float orientation(Vec2 a,Vec2 b,Vec2 c) {
    return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
}

bool within(float value,float a,float b) {
    return value>=std::min(a,b)-1.0e-5f&&value<=std::max(a,b)+1.0e-5f;
}

bool segmentsIntersect(Vec2 a,Vec2 b,Vec2 c,Vec2 d) {
    const float abC=orientation(a,b,c),abD=orientation(a,b,d);
    const float cdA=orientation(c,d,a),cdB=orientation(c,d,b);
    if(((abC>0&&abD<0)||(abC<0&&abD>0))&&
       ((cdA>0&&cdB<0)||(cdA<0&&cdB>0)))return true;
    if(std::fabs(abC)<=1.0e-5f&&within(c.x,a.x,b.x)&&within(c.y,a.y,b.y))return true;
    if(std::fabs(abD)<=1.0e-5f&&within(d.x,a.x,b.x)&&within(d.y,a.y,b.y))return true;
    if(std::fabs(cdA)<=1.0e-5f&&within(a.x,c.x,d.x)&&within(a.y,c.y,d.y))return true;
    return std::fabs(cdB)<=1.0e-5f&&within(b.x,c.x,d.x)&&within(b.y,c.y,d.y);
}

float segmentSegmentDistanceSquared(Vec2 a,Vec2 b,Vec2 c,Vec2 d) {
    if(segmentsIntersect(a,b,c,d))return 0;
    return std::min({pointSegmentDistanceSquared(a,c,d),pointSegmentDistanceSquared(b,c,d),
                     pointSegmentDistanceSquared(c,a,b),pointSegmentDistanceSquared(d,a,b)});
}

float segmentBoxDistanceSquared(Vec2 from,Vec2 to,const NavBox& box) {
    const float left=box.center.x-box.half.x,right=box.center.x+box.half.x;
    const float bottom=box.center.y-box.half.y,top=box.center.y+box.half.y;
    auto inside=[&](Vec2 point) {
        return point.x>=left&&point.x<=right&&point.y>=bottom&&point.y<=top;
    };
    if(inside(from)||inside(to))return 0;
    const Vec2 bl{left,bottom},br{right,bottom},tr{right,top},tl{left,top};
    return std::min({segmentSegmentDistanceSquared(from,to,bl,br),
                     segmentSegmentDistanceSquared(from,to,br,tr),
                     segmentSegmentDistanceSquared(from,to,tr,tl),
                     segmentSegmentDistanceSquared(from,to,tl,bl)});
}

// This is a deliberately brute-force oracle: it scans the authored inputs for
// every query and owns no Navigation cache, grid, attachment, or broadphase.
// Its predicates preserve the public strict-tangent and endpoint semantics.
struct OracleGeometry {
    float worldSize=0;
    std::vector<NavBox> boxes;
    std::vector<NavCircle> circles;

    bool pointClear(Vec2 point,float clearance,Id ignore=0) const {
        if(!finite(point)||!std::isfinite(clearance)||clearance<0||
           !std::isfinite(worldSize)||worldSize<=0||point.x<clearance||point.y<clearance||
           point.x>worldSize-clearance||point.y>worldSize-clearance)return false;
        const float clearanceSquared=clearance*clearance;
        for(const auto& box:boxes) {
            const float dx=std::max(std::fabs(point.x-box.center.x)-box.half.x,0.0f);
            const float dy=std::max(std::fabs(point.y-box.center.y)-box.half.y,0.0f);
            if((dx==0&&dy==0)||dx*dx+dy*dy<clearanceSquared)return false;
        }
        for(const auto& circle:circles) {
            if(ignore&&circle.id==ignore)continue;
            const float combined=clearance+circle.radius;
            if(distanceSquared(point,circle.center)<combined*combined)return false;
        }
        return true;
    }

    bool segmentClear(Vec2 from,Vec2 to,float clearance,Id ignore=0) const {
        if(!pointClear(from,clearance,ignore)||!pointClear(to,clearance,ignore))return false;
        const float clearanceSquared=clearance*clearance;
        for(const auto& box:boxes) {
            const float separation=segmentBoxDistanceSquared(from,to,box);
            if(separation==0||separation<clearanceSquared)return false;
        }
        for(const auto& circle:circles) {
            if(ignore&&circle.id==ignore)continue;
            const float combined=clearance+circle.radius;
            if(pointSegmentDistanceSquared(circle.center,from,to)<combined*combined)return false;
        }
        return true;
    }
};

Navigation navigationFor(const OracleGeometry& geometry) {
    Navigation navigation;
    navigation.sync(geometry.worldSize,geometry.boxes,geometry.circles);
    return navigation;
}

void comparePoint(const Navigation& navigation,const OracleGeometry& oracle,
                  Vec2 point,float clearance,Id ignore,const std::string& context) {
    check(navigation.pointClear(point,clearance,ignore)==oracle.pointClear(point,clearance,ignore),
          context+" pointClear disagreed with brute-force oracle");
}

void compareSegment(const Navigation& navigation,const OracleGeometry& oracle,
                    Vec2 from,Vec2 to,float clearance,Id ignore,const std::string& context) {
    check(navigation.segmentClear(from,to,clearance,ignore)==
          oracle.segmentClear(from,to,clearance,ignore),
          context+" segmentClear disagreed with brute-force oracle");
}

void explicitPredicateEdgesMatchOracle() {
    OracleGeometry oracle{1000,{{{500,300},{50,50}}},{{17,{500,700},50}}};
    const Navigation navigation=navigationFor(oracle);
    const float toward=std::nextafter(560.0f,0.0f);
    const float away=std::nextafter(560.0f,std::numeric_limits<float>::infinity());
    const float boxSegmentInward=std::nextafter(360.0f,0.0f);
    const float circleSegmentInward=std::nextafter(760.0f,0.0f);

    check(oracle.pointClear({560,300},10)&&!oracle.pointClear({toward,300},10)&&
          oracle.pointClear({away,300},10),"box point tangent does not use strict overlap semantics");
    check(oracle.pointClear({560,700},10)&&!oracle.pointClear({toward,700},10)&&
          oracle.pointClear({away,700},10),"circle point tangent does not use strict overlap semantics");
    check(oracle.segmentClear({100,360},{900,360},10)&&
          !oracle.segmentClear({100,boxSegmentInward},{900,boxSegmentInward},10),
          "box segment tangent does not use strict overlap semantics");
    check(oracle.segmentClear({100,760},{900,760},10)&&
          !oracle.segmentClear({100,circleSegmentInward},{900,circleSegmentInward},10),
          "circle segment tangent does not use strict overlap semantics");

    for(const auto& [name,point]:std::vector<std::pair<std::string,Vec2>>{
        {"box tangent",{560,300}},{"box inward ulp",{toward,300}},{"box outward ulp",{away,300}},
        {"circle tangent",{560,700}},{"circle inward ulp",{toward,700}},{"circle outward ulp",{away,700}},
        {"world tangent",{10,100}},{"world outside ulp",{std::nextafter(10.0f,0.0f),100}}
    })comparePoint(navigation,oracle,point,10,0,name);

    compareSegment(navigation,oracle,{100,360},{900,360},10,0,"box segment tangent");
    compareSegment(navigation,oracle,{100,boxSegmentInward},{900,boxSegmentInward},10,0,
                   "box segment inward ulp");
    compareSegment(navigation,oracle,{100,760},{900,760},10,0,"circle segment tangent");
    compareSegment(navigation,oracle,{100,circleSegmentInward},{900,circleSegmentInward},10,0,
                   "circle segment inward ulp");
    for(Vec2 point:std::array<Vec2,3>{{{120,120},{500,300},{560,700}}})
        compareSegment(navigation,oracle,point,point,10,0,"zero-length segment");
    comparePoint(navigation,oracle,{500,700},10,17,"ignored circle point");
    compareSegment(navigation,oracle,{100,700},{900,700},10,17,"ignored circle segment");
}

void invalidAndExtremeInputsMatchOracle() {
    OracleGeometry oracle{1000,{{{500,500},{40,80}}},{{9,{250,250},35}}};
    const Navigation navigation=navigationFor(oracle);
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const float infinity=std::numeric_limits<float>::infinity();
    const float maximum=std::numeric_limits<float>::max();
    for(Vec2 point:std::array<Vec2,8>{{{nan,20},{20,nan},{infinity,20},{20,-infinity},
                                      {maximum,500},{-maximum,500},{0,0},{1000,1000}}})
        comparePoint(navigation,oracle,point,5,0,"invalid or extreme point");
    for(float clearance:std::array<float,5>{{nan,infinity,-infinity,-1.0f,maximum}}) {
        comparePoint(navigation,oracle,{100,100},clearance,0,"invalid clearance");
        compareSegment(navigation,oracle,{100,100},{900,900},clearance,0,"invalid segment clearance");
    }
    compareSegment(navigation,oracle,{nan,100},{900,900},5,0,"invalid segment start");
    compareSegment(navigation,oracle,{100,100},{maximum,900},5,0,"extreme segment end");

    OracleGeometry huge{maximum,{},{{31,{maximum,maximum},1}}};
    const Navigation hugeNavigation=navigationFor(huge);
    comparePoint(hugeNavigation,huge,{maximum*0.5f,maximum*0.5f},1,0,"huge finite world point");
    compareSegment(hugeNavigation,huge,{1,1},{maximum*0.5f,maximum*0.5f},1,0,
                   "huge finite world segment");

    for(float invalidWorld:std::array<float,4>{{0,-1,nan,infinity}}) {
        OracleGeometry invalid{invalidWorld,{},{}};
        const Navigation invalidNavigation=navigationFor(invalid);
        comparePoint(invalidNavigation,invalid,{0,0},0,0,"invalid world");
        compareSegment(invalidNavigation,invalid,{0,0},{0,0},0,0,"invalid world segment");
    }
}

std::uint32_t randomNext(std::uint32_t& state) {
    state^=state<<13;state^=state>>17;state^=state<<5;return state;
}

float randomRange(std::uint32_t& state,float minimum,float maximum) {
    const float unit=static_cast<float>(randomNext(state)&0x00ffffffu)/16777215.0f;
    return minimum+(maximum-minimum)*unit;
}

void fixedSeedPredicateCorpusMatchesOracle() {
    std::uint32_t random=0x51a7c0deu;
    int pointQueries=0,segmentQueries=0;
    for(int fixture=0;fixture<24;++fixture) {
        OracleGeometry oracle;oracle.worldSize=1024;
        for(int index=0;index<8;++index)oracle.boxes.push_back({
            {randomRange(random,40,984),randomRange(random,40,984)},
            {randomRange(random,0,70),randomRange(random,0,70)}});
        for(int index=0;index<12;++index)oracle.circles.push_back({
            static_cast<Id>(100+index),
            {randomRange(random,20,1004),randomRange(random,20,1004)},
            randomRange(random,0,55)});
        const Navigation navigation=navigationFor(oracle);
        for(int query=0;query<160;++query) {
            const float clearances[]{0,0.25f,4,12,24,40};
            const float clearance=clearances[randomNext(random)%6];
            const Id ignore=(randomNext(random)%4)==0
                ? oracle.circles[randomNext(random)%oracle.circles.size()].id:0;
            const Vec2 from{randomRange(random,-80,1104),randomRange(random,-80,1104)};
            std::ostringstream context;context<<"fixture "<<fixture<<" query "<<query;
            comparePoint(navigation,oracle,from,clearance,ignore,context.str());++pointQueries;
            if((query&1)==0) {
                const Vec2 to{randomRange(random,-80,1104),randomRange(random,-80,1104)};
                compareSegment(navigation,oracle,from,to,clearance,ignore,context.str());++segmentQueries;
            }
        }
    }
    std::cout<<"PREDICATE_CORPUS seed=0x51a7c0de fixtures=24 point_queries="<<pointQueries
             <<" segment_queries="<<segmentQueries<<'\n';
}

void adversarialSegmentGeometryMatchesOracle() {
    int comparisons=0;
    auto compare=[&](const Navigation& navigation,const OracleGeometry& oracle,
                     Vec2 from,Vec2 to,float clearance,Id ignore,const char* context) {
        compareSegment(navigation,oracle,from,to,clearance,ignore,context);++comparisons;
    };

    OracleGeometry boxes{1000,{
        {{500,500},{100,60}},{{200,200},{0,0}},{{-25,500},{40,100}},
        {{1025,500},{40,100}},{{700,200},{20,20}},{{700,200},{20,20}}
    },{}};
    const Navigation boxNavigation=navigationFor(boxes);
    const float face=568.0f;
    compare(boxNavigation,boxes,{100,face},{900,face},8,0,"box face tangent");
    compare(boxNavigation,boxes,{100,std::nextafter(face,0.0f)},
            {900,std::nextafter(face,0.0f)},8,0,"box face inward ulp");
    compare(boxNavigation,boxes,{100,std::nextafter(face,std::numeric_limits<float>::infinity())},
            {900,std::nextafter(face,std::numeric_limits<float>::infinity())},8,0,
            "box face outward ulp");
    compare(boxNavigation,boxes,{100,560},{900,560},0,0,"box face at zero clearance");
    compare(boxNavigation,boxes,{100,std::nextafter(560.0f,0.0f)},
            {900,std::nextafter(560.0f,0.0f)},0,0,
            "box face inward ulp at zero clearance");
    compare(boxNavigation,boxes,{100,std::nextafter(560.0f,std::numeric_limits<float>::infinity())},
            {900,std::nextafter(560.0f,std::numeric_limits<float>::infinity())},0,0,
            "box face outward ulp at zero clearance");
    const float zeroCornerLine=1160.0f;
    for(float line:std::array<float,3>{{zeroCornerLine,std::nextafter(zeroCornerLine,0.0f),
        std::nextafter(zeroCornerLine,std::numeric_limits<float>::infinity())}})
        compare(boxNavigation,boxes,{620,line-620},{line-580,580},0,0,
                "box corner zero-clearance ulp family");
    const float cornerLine=1160.0f+8.0f*std::sqrt(2.0f);
    for(float line:std::array<float,3>{{cornerLine,std::nextafter(cornerLine,0.0f),
        std::nextafter(cornerLine,std::numeric_limits<float>::infinity())}})
        compare(boxNavigation,boxes,{600,line-600},{line-560,560},8,0,
                "box corner tangent ulp family");
    compare(boxNavigation,boxes,{100,200},{300,200},0,0,"zero-half box at zero clearance");
    check(!boxes.segmentClear({100,200},{300,200},0),
          "zero-half box lost its legacy exact-intersection behavior");
    compare(boxNavigation,boxes,{100,std::nextafter(200.0f,std::numeric_limits<float>::infinity())},
            {300,std::nextafter(200.0f,std::numeric_limits<float>::infinity())},0,0,
            "zero-half box collinearity epsilon");
    compare(boxNavigation,boxes,{15,300},{15,700},0,0,"left outside-world box face");
    compare(boxNavigation,boxes,{985,300},{985,700},0,0,"right outside-world box face");
    compare(boxNavigation,boxes,{0,560.0001f},{1000,560.0002f},0,0,
            "long nearly parallel box edge");
    compare(boxNavigation,boxes,{0,559.9999f},{1000,560.0001f},0,0,
            "long shallow crossing box edge");

    OracleGeometry circles{1000,{}, {
        {77,{300,300},40},{77,{700,300},25},{88,{500,700},0},
        {91,{-30,500},50},{92,{1030,500},50}
    }};
    const Navigation circleNavigation=navigationFor(circles);
    const float circleFace=348.0f;
    compare(circleNavigation,circles,{100,circleFace},{500,circleFace},8,0,
            "circle tangent");
    compare(circleNavigation,circles,{100,std::nextafter(circleFace,0.0f)},
            {500,std::nextafter(circleFace,0.0f)},8,0,"circle inward ulp");
    compare(circleNavigation,circles,
            {100,std::nextafter(circleFace,std::numeric_limits<float>::infinity())},
            {500,std::nextafter(circleFace,std::numeric_limits<float>::infinity())},8,0,
            "circle outward ulp");
    const float zeroCircleFace=340.0f;
    compare(circleNavigation,circles,{100,zeroCircleFace},{500,zeroCircleFace},0,0,
            "circle tangent at zero clearance");
    compare(circleNavigation,circles,{100,std::nextafter(zeroCircleFace,0.0f)},
            {500,std::nextafter(zeroCircleFace,0.0f)},0,0,
            "circle inward ulp at zero clearance");
    compare(circleNavigation,circles,
            {100,std::nextafter(zeroCircleFace,std::numeric_limits<float>::infinity())},
            {500,std::nextafter(zeroCircleFace,std::numeric_limits<float>::infinity())},0,0,
            "circle outward ulp at zero clearance");
    compare(circleNavigation,circles,{100,300},{900,300},0,77,
            "duplicate circle IDs ignored together");
    compare(circleNavigation,circles,{100,300},{900,300},0,999999,
            "unknown circle ignore changes nothing");
    compare(circleNavigation,circles,{450,700},{550,700},0,0,
            "zero-radius circle at zero clearance");
    check(circles.segmentClear({450,700},{550,700},0),
          "zero-radius circle changed its legacy strict-zero behavior");
    compare(circleNavigation,circles,{450,708},{550,708},8,0,
            "zero-radius circle tangent with clearance");
    compare(circleNavigation,circles,{0,400},{0,600},0,0,
            "outside-world circle overlapping left boundary");
    compare(circleNavigation,circles,{1000,400},{1000,600},0,0,
            "outside-world circle overlapping right boundary");

    OracleGeometry shortGeometry{1,{{{0.1f,0.1f},{0,0}}},{{5,{0.8f,0.8f},0}}};
    const Navigation shortNavigation=navigationFor(shortGeometry);
    const Vec2 shortStart{0.2f,0.2f};
    const Vec2 shortEnd{std::nextafter(shortStart.x,std::numeric_limits<float>::infinity()),shortStart.y};
    compare(shortNavigation,shortGeometry,shortStart,shortStart,0,0,"exact zero-length segment");
    compare(shortNavigation,shortGeometry,shortStart,shortEnd,0,0,
            "sub-epsilon nonzero segment");

    const float hugeWorld=1.0e20f;
    OracleGeometry huge{hugeWorld,{{{5.0e19f,5.0e19f},{1.0e18f,2.0e18f}}},
                        {{301,{2.0e19f,8.0e19f},5.0e17f}}};
    const Navigation hugeNavigation=navigationFor(huge);
    compare(hugeNavigation,huge,{1.0e15f,4.7e19f},{9.9e19f,4.8e19f},1.0e16f,0,
            "very-large finite nearly parallel segment");
    compare(hugeNavigation,huge,{1.0e15f,8.0e19f},{9.9e19f,8.0e19f},5.0e17f,301,
            "very-large finite ignored circle segment");

    const float denormal=std::numeric_limits<float>::denorm_min();
    OracleGeometry tiny{denormal*1024,{{{denormal*512,denormal*512},
                                       {denormal*64,denormal*32}}},
                        {{401,{denormal*256,denormal*768},denormal*16}}};
    const Navigation tinyNavigation=navigationFor(tiny);
    compare(tinyNavigation,tiny,{denormal*8,denormal*480},{denormal*1016,denormal*480},
            denormal*4,0,"denormal box segment");
    compare(tinyNavigation,tiny,{denormal*8,denormal*768},{denormal*1016,denormal*768},
            denormal*4,401,"denormal ignored circle segment");
    check(comparisons==36,"adversarial segment comparison count changed unexpectedly");
    std::cout<<"ADVERSARIAL_SEGMENTS comparisons="<<comparisons<<'\n';
}

void segmentBoxReconstructionEnvelopeMatchesOracle() {
    int interiorComparisons=0;
    auto compareInterior=[&](const OracleGeometry& oracle,Vec2 from,Vec2 to,
                             float clearance,const std::string& context) {
        const Navigation navigation=navigationFor(oracle);
        check(oracle.pointClear(from,clearance)&&oracle.pointClear(to,clearance),
              context+" oracle endpoints are not clear");
        check(navigation.pointClear(from,clearance)&&navigation.pointClear(to,clearance),
              context+" public endpoints are not clear");
        compareSegment(navigation,oracle,from,to,clearance,0,context);
        ++interiorComparisons;
    };
    auto bothDirections=[&](const OracleGeometry& oracle,Vec2 from,Vec2 to,
                            float clearance,const std::string& context) {
        compareInterior(oracle,from,to,clearance,context+" forward");
        compareInterior(oracle,to,from,clearance,context+" reverse");
    };

    const OracleGeometry ordinary{2000,{{{500,500},{100,60}}},{}};
    const Vec2 bl{400,440},br{600,440},tr{600,560},tl{400,560};
    check(segmentsIntersect({500,100},{500,900},bl,br)&&
          segmentsIntersect({100,500},{900,500},br,tr)&&
          segmentsIntersect({500,100},{500,900},tr,tl)&&
          segmentsIntersect({100,500},{900,500},tl,bl),
          "legacy intersection fixture does not cover all four directed box edges");
    // Together these directed rows preserve true legacy intersection decisions
    // for all four reconstructed edges before any envelope rejection.
    bothDirections(ordinary,{100,500},{900,500},0,"legacy horizontal intersections");
    bothDirections(ordinary,{500,100},{500,900},0,"legacy vertical intersections");

    for(float coordinate:std::array<float,3>{{568.0f,std::nextafter(568.0f,0.0f),
        std::nextafter(568.0f,std::numeric_limits<float>::infinity())}})
        bothDirections(ordinary,{100,coordinate},{900,coordinate},8,
                       "ordinary horizontal threshold ulp");
    for(float coordinate:std::array<float,3>{{608.0f,std::nextafter(608.0f,0.0f),
        std::nextafter(608.0f,std::numeric_limits<float>::infinity())}})
        bothDirections(ordinary,{coordinate,100},{coordinate,900},8,
                       "ordinary vertical threshold ulp");
    const float cornerLine=1160.0f+8.0f*std::sqrt(2.0f);
    for(float line:std::array<float,3>{{cornerLine,std::nextafter(cornerLine,0.0f),
        std::nextafter(cornerLine,std::numeric_limits<float>::infinity())}})
        bothDirections(ordinary,{620,line-620},{line-580,580},8,
                       "corner projection threshold ulp");

    const float gate=1.0e12f;
    const float belowGate=std::nextafter(gate,0.0f);
    const float aboveGate=std::nextafter(gate,std::numeric_limits<float>::infinity());
    for(float far:std::array<float,3>{{belowGate,gate,aboveGate}}) {
        OracleGeometry gated{2.0e12f,ordinary.boxes,{}};
        for(float coordinate:std::array<float,3>{{568.0f,std::nextafter(568.0f,0.0f),
            std::nextafter(568.0f,std::numeric_limits<float>::infinity())}}) {
            std::ostringstream context;
            context<<"finite-domain gate far_bits=0x"<<std::hex<<bits(far)
                   <<" line_bits=0x"<<bits(coordinate);
            bothDirections(gated,{100,coordinate},{far,coordinate},8,context.str());
        }
    }

    for(float anchor:std::array<float,4>{{1000.0f,1.0e6f,1.0e9f,gate-1.0e6f}}) {
        const float ulp=std::nextafter(anchor,std::numeric_limits<float>::infinity())-anchor;
        const float half=anchor>1.0e11f?ulp*2:100.0f;
        const float reach=anchor>1.0e11f?ulp*8:400.0f;
        const OracleGeometry scaled{2.0e12f,{{{anchor,500},{half,60}}},{}};
        for(float coordinate:std::array<float,3>{{568.0f,std::nextafter(568.0f,0.0f),
            std::nextafter(568.0f,std::numeric_limits<float>::infinity())}}) {
            std::ostringstream context;
            context<<"translated scale anchor_bits=0x"<<std::hex<<bits(anchor)
                   <<" line_bits=0x"<<bits(coordinate);
            bothDirections(scaled,{anchor-reach,coordinate},{anchor+reach,coordinate},8,
                           context.str());
        }
    }

    auto orderedReconstruct=[](float from,float to) {
        volatile float delta=to-from;
        volatile float result=from+delta;
        return result;
    };
    check(bits(orderedReconstruct(gate,64.0f))!=bits(64.0f),
          "segment reconstruction fixture no longer rounds away its authored endpoint");
    const OracleGeometry segmentCancellation{2.0e12f,ordinary.boxes,{}};
    bothDirections(segmentCancellation,{gate,568},{64,568},8,
                   "segment reconstructed endpoint cancellation");

    NavBox edgeCancellation{};
    bool foundEdgeCancellation=false;
    float half=5.0e11f;
    for(int step=0;step<256&&!foundEdgeCancellation;++step) {
        half=std::nextafter(half,0.0f);
        const NavBox candidate{{5.0e11f,2000},{half,60}};
        const float left=candidate.center.x-candidate.half.x;
        const float right=candidate.center.x+candidate.half.x;
        const float forward=orderedReconstruct(left,right);
        const float reverse=orderedReconstruct(right,left);
        if(left>2048&&std::fabs(left)<=gate&&std::fabs(right)<=gate&&
           (bits(forward)!=bits(right)||bits(reverse)!=bits(left))) {
            edgeCancellation=candidate;
            foundEdgeCancellation=true;
        }
    }
    check(foundEdgeCancellation,
          "bounded directed box-edge reconstruction fixture found no rounding mismatch");
    const float left=edgeCancellation.center.x-edgeCancellation.half.x;
    const float tangent=left-256.0f;
    const OracleGeometry reconstructedBox{2.0e12f,{edgeCancellation},{}};
    for(float coordinate:std::array<float,3>{{tangent,
        std::nextafter(tangent,std::numeric_limits<float>::infinity()),
        std::nextafter(tangent,0.0f)}}) {
        std::ostringstream context;
        context<<"directed box reconstruction x_bits=0x"<<std::hex<<bits(coordinate);
        bothDirections(reconstructedBox,{coordinate,1000},{coordinate,3000},256,context.str());
    }

    for(const auto& endpoints:std::array<std::pair<Vec2,Vec2>,2>{{
        {{100,560.0001f},{1900,560.0002f}},
        {{100,559.9999f},{1900,560.0001f}}
    }})bothDirections(ordinary,endpoints.first,endpoints.second,0,
                      "long nearly-parallel tiny-gap segment");

    check(interiorComparisons>=60,
          "segment-box reconstruction suite lost meaningful interior comparisons");
    std::cout<<"SEGMENT_BOX_RECONSTRUCTION interior_comparisons="<<interiorComparisons<<'\n';
}

void parameterizedGeometryCountsMatchOracle() {
    constexpr std::array<std::pair<int,int>,7> counts{{
        {0,0},{1,0},{0,1},{1,1},{3,7},{17,33},{64,64}
    }};
    std::uint32_t random=0xa771c45du;
    int pointQueries=0,segmentQueries=0;
    for(const auto& [boxCount,circleCount]:counts) {
        OracleGeometry oracle;oracle.worldSize=1536;
        for(int index=0;index<boxCount;++index)oracle.boxes.push_back({
            {randomRange(random,-160,1696),randomRange(random,-160,1696)},
            {index%11?randomRange(random,0.01f,90):0,
             index%13?randomRange(random,0.01f,90):0}});
        for(int index=0;index<circleCount;++index)oracle.circles.push_back({
            static_cast<Id>(500+index%9),
            {randomRange(random,-160,1696),randomRange(random,-160,1696)},
            index%10?randomRange(random,0.01f,75):0});
        const Navigation navigation=navigationFor(oracle);
        for(int query=0;query<96;++query) {
            const float clearances[]{0,0.125f,2,9,31,64};
            const float clearance=clearances[randomNext(random)%6];
            Id ignore=0;
            if(circleCount&&query%5==0)ignore=oracle.circles[static_cast<std::size_t>(query)%
                oracle.circles.size()].id;
            else if(query%11==0)ignore=999999;
            const Vec2 from{randomRange(random,-200,1736),randomRange(random,-200,1736)};
            const Vec2 to{randomRange(random,-200,1736),randomRange(random,-200,1736)};
            std::ostringstream context;
            context<<"counts "<<boxCount<<'/'<<circleCount<<" query "<<query;
            comparePoint(navigation,oracle,from,clearance,ignore,context.str());++pointQueries;
            compareSegment(navigation,oracle,from,to,clearance,ignore,context.str());++segmentQueries;
        }
    }
    check(pointQueries==672&&segmentQueries==672,
          "parameterized predicate comparison count changed unexpectedly");
    std::cout<<"PARAMETERIZED_PREDICATES seed=0xa771c45d layouts="<<counts.size()
             <<" point_queries="<<pointQueries<<" segment_queries="<<segmentQueries<<'\n';
}

struct RouteReport {
    std::uint64_t hash=1469598103934665603ULL;
    int reachedGraph=0;
    std::vector<std::string> lines;
};

void hashInteger(std::uint64_t& hash,std::uint64_t value) {
    for(int byte=0;byte<8;++byte) {
        hash=(hash^static_cast<std::uint8_t>(value&255))*1099511628211ULL;value>>=8;
    }
}

void captureRoute(RouteReport& report,const std::string& name,const NavigationResult& route) {
    if(route.reached&&route.expanded>0)++report.reachedGraph;
    std::uint64_t pathHash=1469598103934665603ULL;
    auto add=[&](std::uint64_t value) {hashInteger(report.hash,value);hashInteger(pathHash,value);};
    add(route.reached);add(route.exhausted);add(static_cast<std::uint32_t>(route.expanded));
    add(bits(route.cost));add(bits(route.origin.x));add(bits(route.origin.y));add(route.points.size());
    for(Vec2 point:route.points){add(bits(point.x));add(bits(point.y));}
    std::ostringstream line;
    line<<"ROUTE_CASE name="<<name<<" reached="<<route.reached<<" exhausted="<<route.exhausted
        <<" expanded="<<route.expanded<<" cost_bits=0x"<<std::hex<<std::setw(8)<<std::setfill('0')
        <<bits(route.cost)<<" origin_bits=0x"<<std::setw(8)<<bits(route.origin.x)<<":0x"
        <<std::setw(8)<<bits(route.origin.y)<<" points="<<std::dec<<route.points.size()
        <<" result_hash=0x"<<std::hex<<std::setw(16)<<pathHash;
    report.lines.push_back(line.str());
}

RouteReport routeCorpus() {
    RouteReport report;
    {
        Navigation navigation;
        navigation.sync(1200,{{{600,600},{35,350}},{{360,180},{120,18}},{{840,1020},{120,18}}},
                        {{17,{600,190},70},{18,{600,1010},55}});
        const std::vector<Vec2> goals{{1040,420},{1040,780},{1040,600}};
        captureRoute(report,"wall-a",navigation.route({160,600},goals,12));
        captureRoute(report,"wall-b-warm",navigation.route({180,550},goals,12));
        captureRoute(report,"wall-clearance",navigation.route({180,550},goals,28));
        captureRoute(report,"wall-ignore",navigation.route({180,550},goals,12,17));
        captureRoute(report,"wall-multi",navigation.routeFromAny({{160,600},{180,550}},goals,12));
        captureRoute(report,"wall-goal-reorder",navigation.route({170,640},{goals[2],goals[0]},12));
    }
    {
        const std::vector<NavBox> wall{{{500,500},{30,300}}};
        const Vec2 start{150,500},goal{850,500};
        Navigation ignoredStart;
        ignoredStart.sync(1000,wall,{{91,start,70}});
        check(!ignoredStart.segmentClear(start,goal,8,91),
              "ignored-start fixture unexpectedly bypasses graph routing");
        const auto fromIgnored=ignoredStart.route(start,{goal},8,91);
        check(fromIgnored.reached&&fromIgnored.expanded>0,
              "route did not recover graph attachments around an ignored start circle");
        captureRoute(report,"ignored-circle-start",fromIgnored);

        Navigation ignoredGoal;
        ignoredGoal.sync(1000,wall,{{92,goal,70}});
        check(!ignoredGoal.segmentClear(start,goal,8,92),
              "ignored-goal fixture unexpectedly bypasses graph routing");
        const auto toIgnored=ignoredGoal.route(start,{goal},8,92);
        check(toIgnored.reached&&toIgnored.expanded>0,
              "route did not recover graph attachments around an ignored goal circle");
        captureRoute(report,"ignored-circle-goal",toIgnored);
    }
    {
        Navigation navigation;
        navigation.sync(768,{
            {{220,250},{18,170}},{{420,520},{18,170}},{{600,250},{18,170}},
            {{320,390},{90,16}},{{520,380},{90,16}}
        },{{41,{100,600},35},{42,{680,650},28},{43,{380,100},42}});
        const std::vector<Vec2> goals{{700,700},{690,80},{620,430},{90,690}};
        captureRoute(report,"maze-a",navigation.route({70,70},goals,6));
        captureRoute(report,"maze-b-warm",navigation.route({90,100},goals,6));
        captureRoute(report,"maze-wide",navigation.route({90,100},goals,18));
        captureRoute(report,"maze-ignore",navigation.route({90,100},goals,6,43));
        captureRoute(report,"maze-multi",navigation.routeFromAny({{70,70},{360,700},{700,350}},goals,6));
        captureRoute(report,"maze-zero",navigation.route({90,690},{{90,690}},6));
    }
    {
        Navigation navigation;
        navigation.sync(1400,{
            {{700,700},{22,520}},{{350,360},{210,20}},{{1050,1040},{210,20}}
        },{{70,{260,1040},80},{71,{1140,360},80},{72,{700,110},48},{73,{700,1290},48}});
        const std::vector<Vec2> goals{{1260,250},{1260,700},{1260,1150},{900,1260}};
        captureRoute(report,"arena-a",navigation.route({140,700},goals,10));
        captureRoute(report,"arena-b-warm",navigation.route({160,740},goals,10));
        captureRoute(report,"arena-wide",navigation.route({160,740},goals,30));
        captureRoute(report,"arena-ignore",navigation.route({160,740},goals,10,70));
        captureRoute(report,"arena-multi",navigation.routeFromAny({{140,700},{500,1200}},goals,10));
        const float nan=std::numeric_limits<float>::quiet_NaN();
        captureRoute(report,"arena-invalid",navigation.route({nan,700},goals,10));
        captureRoute(report,"arena-no-goals",navigation.route({140,700},{},10));
    }
    return report;
}

void routeCorpusHasStableComparableFingerprint() {
    const RouteReport first=routeCorpus(),second=routeCorpus();
    check(first.hash==second.hash&&first.lines==second.lines&&
          first.reachedGraph==second.reachedGraph,
          "fixed route corpus changed between fresh deterministic runs");
    check(first.lines.size()==21,"route corpus case count changed unexpectedly");
    // Clearance-aware arc sampling intentionally changes routes, repairing
    // disconnected obstacle boundaries (see NavigationRouteQualityTests).
    // The earlier predicate-optimization receipt remains in
    // artifacts/latency/predicates-before.log (0x0a097241ddad648f).
    constexpr std::uint64_t ApprovedBaselineHash=0xf79c3058a531a44dULL;
    check(first.reachedGraph==17,
          "route corpus changed its approved reached graph-search count");
    for(const auto& line:first.lines)std::cout<<line<<'\n';
    std::cout<<"ROUTE_CORPUS cases="<<first.lines.size()<<" hash=0x"<<std::hex
             <<std::setw(16)<<std::setfill('0')<<first.hash<<std::dec
             <<" reached_graph="<<first.reachedGraph<<'\n';
    check(first.hash==ApprovedBaselineHash,
          "route corpus changed its approved exact-result baseline hash");
}

} // namespace

int main() {
    const std::vector<std::pair<std::string,std::function<void()>>> tests{
        {"explicit predicate edges match oracle",explicitPredicateEdgesMatchOracle},
        {"invalid and extreme inputs match oracle",invalidAndExtremeInputsMatchOracle},
        {"fixed-seed predicate corpus matches oracle",fixedSeedPredicateCorpusMatchesOracle},
        {"adversarial segment geometry matches oracle",adversarialSegmentGeometryMatchesOracle},
        {"segment-box reconstruction envelope matches oracle",segmentBoxReconstructionEnvelopeMatchesOracle},
        {"parameterized geometry counts match oracle",parameterizedGeometryCountsMatchOracle},
        {"route corpus has stable comparable fingerprint",routeCorpusHasStableComparableFingerprint},
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
