#ifndef SECTION_PLAN_H
#define SECTION_PLAN_H

struct SectionPlan {
  static constexpr int MIN_COUNT = 6;
  static constexpr int MAX_COUNT = 60;
  int count = 0;
  float weights[MAX_COUNT] = {};
  float percentages[MAX_COUNT] = {};
  float widthsCm[MAX_COUNT] = {};
  float boundariesCm[MAX_COUNT + 1] = {};
  float midpointsCm[MAX_COUNT] = {};

  bool generate(float spanCm, int sectionCount);
  static int maxValidCount(float spanCm);
};

#endif
