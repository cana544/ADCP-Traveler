#include "section_plan.h"
#include <cmath>

bool SectionPlan::generate(float spanCm, int sectionCount) {
  count = 0;
  if (!std::isfinite(spanCm) || spanCm <= 0 ||
      sectionCount < MIN_COUNT || sectionCount > MAX_COUNT) return false;
  double totalWeight = 0;
  for (int i = 0; i < sectionCount; ++i) {
    const double t = std::fabs((2.0 * (i + 1) - sectionCount - 1) / (sectionCount - 1));
    const double edge = t > 0.8 ? t - 0.8 : 0;
    weights[i] = static_cast<float>(1 + 20 * edge * edge);
    totalWeight += weights[i];
  }
  double boundary = 0;
  boundariesCm[0] = 0;
  for (int i = 0; i < sectionCount; ++i) {
    const double fraction = weights[i] / totalWeight;
    percentages[i] = static_cast<float>(100 * fraction);
    widthsCm[i] = static_cast<float>(fraction * spanCm);
    if (widthsCm[i] < 10.0f) return false;
    boundary += fraction * spanCm;
    boundariesCm[i + 1] = static_cast<float>(boundary);
    midpointsCm[i] = (boundariesCm[i] + boundariesCm[i + 1]) / 2;
  }
  boundariesCm[sectionCount] = spanCm;
  count = sectionCount;
  return true;
}

int SectionPlan::maxValidCount(float spanCm) {
  SectionPlan candidate;
  int maximum = 0;
  for (int n = MIN_COUNT; n <= MAX_COUNT; ++n)
    if (candidate.generate(spanCm, n)) maximum = n;
  return maximum;
}
