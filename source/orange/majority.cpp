/*
    This file is part of Orange.

    Orange is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Orange is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Orange; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Authors: Janez Demsar, Blaz Zupan, 1996--2002
    Contact: janez.demsar@fri.uni-lj.si
*/


#include <math.h>

#include "random.hpp"
#include "vars.hpp"
#include "domain.hpp"
#include "distvars.hpp"
#include "examples.hpp"
#include "examplegen.hpp"
#include "table.hpp"
#include "classify.hpp"

#include "measures.hpp"
#include "estimateprob.hpp"
#include "cost.hpp"

#include "majority.ppp"


TMajorityLearner::TMajorityLearner()
{}


PClassifier TMajorityLearner::operator()(PExampleGenerator ogen, const int &weight)
{ if (!ogen->domain->classVar)
    raiseError("class-less domain");

  PDistribution classDistr = getClassDistribution(ogen, weight);
  if (estimatorConstructor)
    classDistr = estimatorConstructor->call(classDistr, aprioriDistribution, ogen, weight)->call();
    if (!classDistr)
      raiseError("invalid estimatorConstructor");
  else
    classDistr->normalize();

  return mlnew TDefaultClassifier(ogen->domain->classVar,
                                  classDistr->supportsContinuous ? TValue(classDistr->average()) : classDistr->highestProbValue(classDistr->cases),
                                  classDistr);
}

  
TCostLearner::TCostLearner(PCostMatrix acost)
: cost(acost)
{}


PClassifier TCostLearner::operator()(PExampleGenerator gen, const int &weight)
{ if (!gen->domain->classVar)
    raiseError("class-less domain");

  if (gen->domain->classVar->varType!=TValue::INTVAR)
    raiseError("cost-sensitive learning for continuous classes not supported");
  checkProperty(cost);
  
  PClassifier clsfr = TMajorityLearner::operator()(gen, weight);
  float missclassificationCost;
  TMeasureAttribute_cost(cost).majorityCost(clsfr.AS(TDefaultClassifier)->defaultDistribution,
                                                     missclassificationCost,
                                                     clsfr.AS(TDefaultClassifier)->defaultVal);
  return clsfr;
}



TRandomLearner::TRandomLearner()
{}


TRandomLearner::TRandomLearner(PDistribution dist)
: probabilities(dist)
{}


PClassifier TRandomLearner::operator()(PExampleGenerator gen, const int &weight)
{
  return new TRandomClassifier(probabilities ? probabilities : getClassDistribution(gen, weight));
}
