// Trace the real production controller against the existing synthetic plant.
// This model is deliberately uncalibrated: it is not a towing prediction.
#define main regressionMain
#include "test_control.cpp"
#undef main
#include <iomanip>

int main() {
  std::cout << std::setprecision(9);
  std::cout << "scenario,direction,adapt,repeat,time_s,position_cm,encoder_cm,target_cm,velocity_cm_s,estimated_cm_s,command_cm_s,applied_v,load_equiv_v,phase,failure,recoveries,learned_start_v,learned_load_v,learning\n";
  for (const char* scenario : {"constant_load", "varying_resistance", "load_step", "endpoint_friction", "temporary_block", "permanent_block", "assisting_load"}) {
    for (int direction : {1,-1}) for (bool adaptation : {false,true}) {
      Plant p; p.control.setAdaptationEnabled(adaptation);
      for (int repeat=1;repeat<=3;++repeat) {
        float startX=p.x;
        p.start(100,direction);
        uint32_t startUs=fakeUs, terminalUs=0, blockUs=0;
        bool reachedTerminal=false;
        for (int step=0;step<125000;++step) {
          const float progress=p.x-startX;
          std::string name(scenario);
          p.load=2.5f; p.blocked=false; p.terminalFriction=false;
          if(name=="varying_resistance") p.load=2.0f+1.5f*(1+cosf(2*PI*progress/100))*.5f;
          if(name=="load_step" && progress>=25) p.load=4.2f;
          if(name=="endpoint_friction" && progress>=95) p.load=5.0f;
          if((name=="temporary_block" || name=="permanent_block") && progress>=40 && !blockUs) blockUs=fakeUs;
          if(blockUs && (name=="permanent_block" || uint32_t(fakeUs-blockUs)<4000000UL)) p.blocked=true;
          if(name=="assisting_load") p.load=-1;
          p.tick();
          if(!p.control.isActive() && !reachedTerminal) { terminalUs=fakeUs;reachedTerminal=true; }
          if(p.ticks%50==0) {
            const auto t=p.control.telemetry();
            std::cout<<scenario<<','<<direction<<','<<adaptation<<','<<repeat<<','<<uint32_t(fakeUs-startUs)*1e-6
              <<','<<p.x-startX<<','<<direction*(p.encoder.positionCm()-p.control.moveStartPositionCm())
              <<",100,"<<p.v<<','<<direction*p.encoder.velocityCmS()<<','<<direction*t.commandCmS
              <<','<<fabsf(p.motor.currentSpeed()*12.f/255)<<','<<p.load<<','<<p.control.phaseText()
              <<','<<p.control.failureText()<<','<<int(t.recoveries)<<','<<p.control.learnedStartVoltage(direction)
              <<','<<p.control.learnedLoadVoltage(direction)<<','<<t.learningEligible<<'\n';
          }
          if(reachedTerminal && uint32_t(fakeUs-terminalUs)>=3000000UL) break;
        }
        // Fault cases are single attempts; a new request after them is not part of this trace.
        if(p.control.state()!=DistanceController::State::COMPLETE) break;
      }
    }
  }
}
