#include <algorithm>
#include "table.hpp"
#include "module.hpp"
#include "errors.hpp"
#include "cls_orange.hpp"
#include "cls_example.hpp"

#include "heatmap.ppp"

#include "externs.px"

#ifdef _MSC_VER
  #pragma warning (disable : 4786 4114 4018 4267 4244)
#endif

DEFINE_TOrangeVector_classDescription(PHeatmap, "THeatmapList")

class CompareIndicesWClass {
public:
  const vector<float> &centers;
  const vector<int> &classes;

  CompareIndicesWClass(const vector<float> &acen, const vector<int> &acl)
  : centers(acen),
    classes(acl)
  {};

  bool operator() (const int &i1, const int &i2)
  { return    (classes[i1]<classes[i2])
           || ((classes[i1] == classes[i2]) && (centers[i1] < centers[i2])); }
};


class CompareIndices {
public:
  const vector<float> &centers;

  CompareIndices(const vector<float> &acen)
  : centers(acen)
  {};

  bool operator() (const int &i1, const int &i2)
  { return centers[i1] < centers[i2]; }
};


WRAPPER(ExampleTable);

#define UNKNOWN_F -1e30f

THeatmap::THeatmap(const int &h, const int &w, PExampleTable ex)
: bitmap(new unsigned char [h*w]),
  averages(new unsigned char[h]),
  height(h),
  width(w),
  examples(ex),
  exampleIndices(new TIntList())
{}


THeatmap::~THeatmap()
{
  delete bitmap;
  delete averages;
}

/* Expends the bitmap 
   Each pixel in bitmap 'smmp' is replaced by a square with
     given 'cellWidth' and 'cellHeight'
   The original bitmaps width and height are given by arguments
     'width' and 'height'

   Beside returning the bitmap, the function return its size
   in bytes (argument '&size'). Due to alignment of rows to 4 bytes,
   this does not necessarily equal cellWidth * cellHeight * width * height.
*/

unsigned char *bitmap2string(const int &cellWidth, const int &cellHeight, int &size, unsigned char *smmp, const int &width, const int &height)
{
  const int lineWidth = width * cellWidth;
  const int fill = (4 - lineWidth & 3) & 3;
  const int rowSize = lineWidth + fill;
  size = rowSize * height * cellHeight;

  unsigned char *res = new unsigned char[size];
  unsigned char *resi = res;

  for(int line = 0; line<height; line++) {
    unsigned char *thisline = resi;
    int xpoints, inpoints;
    for(xpoints = width; xpoints--; smmp++)
      for(inpoints = cellWidth; inpoints--; *(resi++) = *smmp);
    resi += fill;
    for(xpoints = cellHeight-1; xpoints--; resi += rowSize)
      memcpy(resi, thisline, rowSize);
  }

  return res;
}


THeatmapConstructor::THeatmapConstructor(PExampleTable table, PHeatmapConstructor baseHeatmap, bool noSorting)
: sortedExamples(new TExampleTable(table, 1)), // lock, but do not copy
  floatMap(),
  classBoundaries(),
  lineCenters(),
  nColumns(table->domain->attributes->size()),
  nRows(table->numberOfExamples()),
  nClasses(0)
{
  TExampleTable &etable = table.getReference();
  if (baseHeatmap && (etable.numberOfExamples() != baseHeatmap->sortedExamples->numberOfExamples()))
    raiseError("'baseHeatmap has a different number of spots");

  TExampleTable &esorted = sortedExamples.getReference();

  bool haveBase = baseHeatmap;

  PITERATE(TVarList, ai, etable.domain->attributes)
    if ((*ai)->varType != TValue::FLOATVAR)
      raiseError("data contains a discrete attribute '%s'", (*ai)->name.c_str());

  if (etable.domain->classVar) {
    if (etable.domain->classVar->varType != TValue::INTVAR)
      raiseError("class attribute is not discrete");
    nClasses = etable.domain->classVar->noOfValues();
  }
  else
    nClasses = 0;

  vector<float *> tempFloatMap;
  vector<float> tempLineCenters;
  vector<float> tempLineAverages;
  vector<int>classes;

  tempFloatMap.reserve(nRows);

  tempLineCenters.reserve(nRows);
  if (!haveBase)
    sortIndices.reserve(nRows);

  try {
    // Extract the data from the table, compute the centers and fill the sortIndices
    EITERATE(ei, etable) {
      if (!haveBase)
        sortIndices.push_back(sortIndices.size());

      float *i_floatMap = new float[nColumns];
      tempFloatMap.push_back(i_floatMap);

      if (nClasses) {
        TValue &classVal = (*ei).getClass();
        classes.push_back(classVal.isSpecial() ? 999 : classVal.intV);
      }
      
      TExample::const_iterator eii((*ei).begin());

      float sumBri = 0.0;
      float sumBriX = 0.0;
      int sumX = 0;
      int N = 0;
      float thismax = -1e30f;
      float thismin = 1e30f;
      
      float *rai = i_floatMap;
      for(int xpoint = 0; xpoint<nColumns; rai++, eii++, xpoint++) {
        if ((*eii).isSpecial()) {
          *rai = UNKNOWN_F;
        }
        else {
          *rai = (*eii).floatV;
          sumBri += *rai;
          sumBriX += *rai * xpoint;
          sumX += xpoint;
          N += 1;
          if (*rai > thismax)
            thismax = *rai;
          if (*rai < thismin)
            thismin = *rai;
        }
      }

      tempLineAverages.push_back(N ? sumBri/N : UNKNOWN_F);
      tempLineCenters.push_back(N && (thismax != thismin) ? (sumBriX - thismin * sumX) / (sumBri - thismin * N) : UNKNOWN_F);
    }

    if (haveBase) {
      sortIndices = baseHeatmap->sortIndices;
      classBoundaries = baseHeatmap->classBoundaries;
    }

    else if (!noSorting) {
      if (nClasses) {
        CompareIndicesWClass compare(tempLineCenters, classes);
        sort(sortIndices.begin(), sortIndices.end(), compare);
      }
      else {
        CompareIndices compare(tempLineCenters);
        sort(sortIndices.begin(), sortIndices.end(), compare);
      }
    }

    floatMap.reserve(nRows);
    lineCenters.reserve(nRows);
    lineAverages.reserve(nRows);
        
    int pcl = -1;
    ITERATE(vector<int>, si, sortIndices) {
      esorted.addExample(etable[*si]);
      lineCenters.push_back(tempLineCenters[*si]);
      lineAverages.push_back(tempLineAverages[*si]);
      floatMap.push_back(tempFloatMap[*si]);
      tempFloatMap[*si] = NULL;
      if (!haveBase && nClasses && (classes[*si] != pcl)) {
        classBoundaries.push_back(floatMap.size());
        pcl = classes[*si];
      }
    }

    if (!haveBase) {
      if (!nClasses)
        classBoundaries.push_back(0);
      classBoundaries.push_back(floatMap.size());
    }
  }
  catch (...) {
    ITERATE(vector<float *>, tfmi, tempFloatMap)
      delete *tfmi;
    ITERATE(vector<float *>, fmi, floatMap)
      delete *fmi;
    throw;
  }
}


THeatmapConstructor::~THeatmapConstructor()
{
  ITERATE(vector<float *>, fmi, floatMap)
    delete *fmi;
}


PHeatmapList THeatmapConstructor::operator ()(const float &unadjustedSqueeze, const float &lowerBound, const float &upperBound, const float &agamm)
{
  bool adjustMinMax =  (lowerBound==0) && (upperBound==0);
  float &abslow = absLow;
  float &abshigh = absHigh;
  if (adjustMinMax) {
    abshigh = -1e30f;
    abslow = 1e30f;
  }
  else {
    abslow = lowerBound;
    abshigh = upperBound;
  }
    
  int ncl = nClasses ? nClasses : 1;
  float **floatMaps = new float *[ncl];
  float **averageMaps = new float *[ncl];
  
  float **fmi = floatMaps;
  float **ami = averageMaps;
  int classNo;

  PHeatmapList hml = new THeatmapList;

  int *spec = new int[nColumns];
  
  for(classNo = 0; classNo < ncl; classNo++, fmi++) {
    const int classBegin = classBoundaries[classNo];
    const int classEnd = classBoundaries[classNo+1];

    int nLines = int(floor(0.5 + (classEnd - classBegin) * unadjustedSqueeze));

    THeatmap *hm = new THeatmap(nLines, nColumns, sortedExamples);
    hml->push_back(hm);

    const float squeeze = float(nLines) / (classEnd-classBegin);

    float *fm1i = *fmi = new float [nLines * nColumns]; // that's the space for floatmap for one class
    float *am1i = *ami = new float [nLines]; // merged line averages

    float inThisRow = 0;
    float *ri, *fri;
    int *si;
    int xpoint;
    vector<float>::const_iterator lavi(lineAverages.begin());

    int exampleIndex = classBegin;
    hm->exampleIndices->push_back(exampleIndex);

    for(vector<float *>::iterator rowi = floatMap.begin()+classBegin, rowe = floatMap.begin()+classEnd; rowi!=rowe; nLines--, inThisRow-=1.0, am1i++) {
      for(xpoint = nColumns, ri = fm1i, si = spec; xpoint--; *(ri++) = 0, *(si++) = 0);
      float lineAverage = 0.0;
      int nDefinedAverages = 0;

      for(; (rowi != rowe) && ((inThisRow < 1.0) || (nLines==1)); inThisRow += squeeze, rowi++, exampleIndex++, lavi++) {
        for(xpoint = nColumns, fri = *rowi, ri = fm1i, si = spec; xpoint--; fri++, ri++, si++)
          if (*fri != UNKNOWN_F) {
            *ri += *fri;
            (*si)++;
          }

        if (*lavi != UNKNOWN_F) {
          lineAverage += *lavi;
          nDefinedAverages++;
        }
      }

      hm->exampleIndices->push_back(exampleIndex);
      for(xpoint = nColumns, si = spec; xpoint--; fm1i++, si++) {
        if (*si) {
          *fm1i = *fm1i / *si;
          if (adjustMinMax) {
            if (*fm1i < abslow)
              abslow = *fm1i;
            if (*fm1i > abshigh)
              abshigh = *fm1i;
          }
        }
        else
          *fm1i = UNKNOWN_F;
      }

      *am1i = nDefinedAverages ? lineAverage/nDefinedAverages : UNKNOWN_F;
    }

  }

  delete spec;

  gamma = agamm;
  bool gammaIs1 = (agamm == 1.0);
  if (gammaIs1) {
    colorBase = abslow;
    colorFact = 249.0/(abshigh - abslow);
  }
  else {
    colorBase = (abslow + abshigh) / 2;
    colorFact = 2 / (abshigh - abslow);
  }

  fmi = floatMaps;
  ami = averageMaps;
  vector<float>::const_iterator ai(lineAverages.begin());

  PITERATE(THeatmapList, hmi, hml) {
    float *fm1i;
    unsigned char *bmi;
    int idx;

    if (gammaIs1) {
      for(fm1i = *fmi, bmi = (*hmi)->bitmap,   idx = (*hmi)->height * (*hmi)->width; idx--; *(bmi++) = computePixelGamma1(*(fm1i++)));
      for(fm1i = *ami, bmi = (*hmi)->averages, idx = (*hmi)->height;                 idx--; *(bmi++) = computePixelGamma1(*(fm1i++)));
    }
    else {
      for(fm1i = *fmi, bmi = (*hmi)->bitmap,   idx = (*hmi)->height * (*hmi)->width; idx--; *(bmi++) = computePixel(*(fm1i++)));
      for(fm1i = *ami, bmi = (*hmi)->averages, idx = (*hmi)->height;                 idx--; *(bmi++) = computePixel(*(fm1i++)));
    }

    delete *(fmi++);
    delete *(ami++);
  }

  delete floatMaps;
  delete averageMaps;

  return hml;
}


unsigned char *THeatmapConstructor::getLegend(const int &width, const int &height, int &size) const
{
  unsigned char *legendbmp = new unsigned char[width];
  unsigned char *lbmpi = legendbmp;
  float intwid = (absHigh - absLow) / (width-1);
  if (gamma == 1.0)
    for(int wi = 0; wi<width; *(lbmpi++) = computePixelGamma1(absLow + (wi++) * intwid));
  else
    for(int wi = 0; wi<width; *(lbmpi++) = computePixel(absLow + (wi++) * intwid));

  unsigned char *legend = bitmap2string(1, height, size, legendbmp, width, 1);
  delete legendbmp;
  return legend;
}
