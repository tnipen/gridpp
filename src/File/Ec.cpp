#include "Ec.h"
#include <math.h>
#include <assert.h>
#include <stdlib.h>
#include "../Util.h"

FileEc::FileEc(std::string iFilename, bool iReadOnly) : FileNetcdf(iFilename, iReadOnly) {
   // Set dimensions
   NcDim* dTime = getDim("time");
   NcDim* dEns  = getDim("ensemble_member");
   NcDim* dLon  = getDim("longitude");
   NcDim* dLat  = getDim("latitude");
   mNTime = dTime->size();
   mNEns  = dEns->size();
   mNLat  = dLat->size();
   mNLon  = dLon->size();

   // Retrieve lat/lon/elev
   long countLat = getNumLat();
   long countLon = getNumLon();
   long countElev[3] = {1,getNumLat(), getNumLon()};
   float* lats = new float[getNumLat()];
   float* lons = new float[getNumLon()];
   float* elevs = new float[getNumLat()*getNumLon()];
   NcVar* vLat = getVar("latitude");
   NcVar* vLon = getVar("longitude");
   NcVar* vElev = getVar("altitude");
   vLat->get(lats, countLat);
   vLon->get(lons, countLon);
   vElev->get(elevs, countElev);
   mLats.resize(getNumLat());
   mLons.resize(getNumLat());
   mElevs.resize(getNumLat());
   for(int i = 0; i < getNumLat(); i++) {
      mLats[i].resize(getNumLon());
      mLons[i].resize(getNumLon());
      mElevs[i].resize(getNumLon());
      for(int j = 0; j < getNumLon(); j++) {
         mLats[i][j] = lats[i];
         mLons[i][j] = lons[i];
         mElevs[i][j] = elevs[i*getNumLon()+j];
      }
   }

   Util::status( "File '" + iFilename + " 'has dimensions " + getDimenionString());
}

FieldPtr FileEc::getFieldCore(Variable::Type iVariable, int iTime) const {
   std::string variable = getVariableName(iVariable);
   // Not cached, retrieve data
   NcVar* var = getVar(variable);
   int nTime = mNTime;
   int nEns  = mNEns;
   int nLat  = mNLat;
   int nLon  = mNLon;

   long count[5] = {1, 1, nEns, nLat, nLon};
   float* values = new float[nTime*1*nEns*nLat*nLon];
   var->set_cur(iTime, 0, 0, 0, 0);
   var->get(values, count);
   float MV = getMissingValue(var);

   float offset = getOffset(var);
   float scale = getScale(var);
   int index = 0;
   FieldPtr field = getEmptyField();
   for(int e = 0; e < nEns; e++) {
      for(int lat = 0; lat < nLat; lat++) {
         for(int lon = 0; lon < nLon; lon++) {
            float value = values[index];
            if(Util::isValid(MV) && value == MV) {
               // Field has missing value indicator and the value is missing
               // Save values using our own internal missing value indicator
               value = Util::MV;
            }
            else {
               value = scale*values[index] + offset;
            }
            (*field)[lat][lon][e] = value;
            index++;
         }
      }
   }
   delete[] values;
   return field;
}

void FileEc::writeCore(std::vector<Variable::Type> iVariables) {
   for(int v = 0; v < iVariables.size(); v++) {
      Variable::Type varType = iVariables[v];
      std::string variable = getVariableName(varType);
      NcVar* var;
      if(hasVariable(varType)) {
         var = getVar(variable);
      }
      else {
         // Create variable
         NcDim* dTime    = getDim("time");
         NcDim* dSurface = getDim("surface");
         NcDim* dEns     = getDim("ensemble_member");
         NcDim* dLon     = getDim("longitude");
         NcDim* dLat     = getDim("latitude");
         var = mFile.add_var(variable.c_str(), ncFloat, dTime, dSurface, dEns, dLat, dLon);
      }
      float MV = getMissingValue(var); // The output file's missing value indicator
      for(int t = 0; t < mNTime; t++) {
         float offset = getOffset(var);
         float scale = getScale(var);
         FieldPtr field = getField(varType, t);
         if(field != NULL) { // TODO: Can't be null if coming from reference
            var->set_cur(t, 0, 0, 0, 0);
            float* values = new float[mNTime*1*mNEns*mNLat*mNLon];

            int index = 0;
            for(int e = 0; e < mNEns; e++) {
               for(int lat = 0; lat < mNLat; lat++) {
                  for(int lon = 0; lon < mNLon; lon++) {
                     float value = (*field)[lat][lon][e];
                     if(Util::isValid(MV) && !Util::isValid(value)) {
                        // Field has missing value indicator and the value is missing
                        // Save values using the file's missing indicator value
                        value = MV;
                     }
                     else {
                        value = ((*field)[lat][lon][e] - offset)/scale;
                     }
                     values[index] = value;
                     index++;
                  }
               }
            }
            var->put(values, 1, 1, mNEns, mNLat, mNLon);
         }
      }
   }
}


std::string FileEc::getVariableName(Variable::Type iVariable) const {
   if(iVariable == Variable::PrecipAcc) {
      return "precipitation_amount_acc";
   }
   else if(iVariable == Variable::Cloud) {
      return "cloud_area_fraction";
   }
   else if(iVariable == Variable::T) {
      return "t";
   }
   else if(iVariable == Variable::Precip) {
      return "precipitation_amount";
   }
   return "";
}

bool FileEc::isValid(std::string iFilename) {
   bool status = false;
   NcFile file = NcFile(iFilename.c_str(), NcFile::ReadOnly);
   if(file.is_valid()) {
      status = hasDim(file, "time") && hasDim(file, "ensemble_member") && hasDim(file, "longitude") && hasDim(file, "latitude") &&
               hasVar(file, "latitude") && hasVar(file, "longitude");
      file.close();
   }
   return status;
}

vec2 FileEc::getLats() const {
   return mLats;
}
vec2 FileEc::getLons() const {
   return mLons;
}
vec2 FileEc::getElevs() const {
   return mElevs;
}
vec2 FileEc::getLatLonVariable(std::string iVar) const {
   NcVar* var = getVar(iVar);
   long count[2] = {getNumLat(), getNumLon()};
   float* values = new float[getNumLon()*getNumLat()];
   var->get(values, count);
   vec2 grid;
   grid.resize(getNumLat());
   for(int i = 0; i < getNumLat(); i++) {
      grid[i].resize(getNumLon());
      for(int j = 0; j < getNumLon(); j++) {
         int index = i*getNumLon() + j;
         grid[i][j] = values[index];
         assert(index < getNumLon()*getNumLat());
      }
   }
   delete[] values;
   return grid;
}