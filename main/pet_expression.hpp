#pragma once
#include <cmath>
#include <cstdint>
#include "pet_motion.hpp"

enum class PetExpression : uint8_t {
 Happy,Talking,Laugh,Surprised,Shy,Sad,Angry,Sleepy,Wink,Dizzy
};

struct PetExpressionView {
 PetExpression expression=PetExpression::Happy;
 float mouth_open=0.f;
 bool animating=false;
};

// Main-task owned, and deliberately independent of display/wireless activity.
// The caller may draw transient expressions, but must never feed them back to
// the display's idle timer. Silent scenes settle to a completely static frame.
class PetExpressionState {
public:
 const PetExpressionView &update(uint32_t now,bool listening,float rms,
                                const PetMotionView &motion,uint32_t idle_ms) {
  if(!started_){started_=true;started_at_=last_update_=now;}
  const uint32_t elapsed=uint32_t(now-last_update_);last_update_=now;
  const float dt=std::fmin(0.10f,elapsed*0.001f);
  const float signal=listening && std::isfinite(rms)?unit((rms-0.006f)*18.f):0.f;
  const float tau=signal>view_.mouth_open?0.06f:0.16f;
  if(elapsed>1000u)view_.mouth_open=signal;
  else view_.mouth_open+=(signal-view_.mouth_open)*(1.f-std::exp(-dt/tau));
  if(signal==0.f && view_.mouth_open<0.004f)view_.mouth_open=0.f;

  const float anger=unit(motion.angry),dizziness=unit(motion.dizzy),impact=unit(motion.impact);
  const bool angry=anger>0.08f,dizzy=dizziness>0.40f;
  if(angry || dizzy){
   reaction_=true;had_anger_=had_anger_||angry;recovering_=false;
  }else if(reaction_){
   reaction_=false;recovering_=true;recovery_at_=now;
  }
  if(impact>0.28f && impact>previous_impact_+0.08f){surprised_=true;surprise_at_=now;}
  previous_impact_=impact;
  if(surprised_ && uint32_t(now-surprise_at_)>=450u)surprised_=false;
  if(recovering_ && uint32_t(now-recovery_at_)>=1900u){recovering_=false;had_anger_=false;}

  if(angry)view_.expression=PetExpression::Angry;
  else if(dizzy)view_.expression=PetExpression::Dizzy;
  else if(listening || view_.mouth_open>0.f){
   // The laughing mouth is a delighted, strong-voice pose, not speech or
   // sentiment recognition. A silent held key keeps the small closed smile.
   view_.expression=view_.mouth_open>0.62f?PetExpression::Laugh:
    view_.mouth_open>0.025f?PetExpression::Talking:PetExpression::Happy;
  }else if(surprised_)view_.expression=PetExpression::Surprised;
  else if(recovering_)view_.expression=had_anger_ && uint32_t(now-recovery_at_)<650u
    ?PetExpression::Sad:PetExpression::Shy;
  else if(idle_ms>=120000u)view_.expression=PetExpression::Sleepy;
  else {
   const uint32_t age=uint32_t(now-started_at_);
   const bool wink=age>=9000u && (age-9000u)%19000u<650u;
   view_.expression=wink?PetExpression::Wink:PetExpression::Happy;
  }
  view_.animating=view_.mouth_open>0.f || angry || dizzy ||
   view_.expression==PetExpression::Surprised || view_.expression==PetExpression::Sad ||
   view_.expression==PetExpression::Shy || view_.expression==PetExpression::Wink;
  return view_;
 }

 const PetExpressionView &view() const {return view_;}

private:
 PetExpressionView view_;
 bool started_=false,reaction_=false,had_anger_=false,recovering_=false,surprised_=false;
 uint32_t started_at_=0,last_update_=0,recovery_at_=0,surprise_at_=0;
 float previous_impact_=0.f;
 static float unit(float value){return std::isfinite(value)?std::fmin(1.f,std::fmax(0.f,value)):0.f;}
};
