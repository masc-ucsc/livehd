#pragma once

#include <iostream>
#include <string>

// Temporary local for crazy mirror reflection stuff.
constexpr int maxMirrorDepth = 20;

extern bool   xReflect;
extern double xLeft[];
extern double xRight[];
extern int    xMirrorDepth;
extern bool   yReflect;
extern double yBottom[];
extern double yTop[];
extern int    yMirrorDepth;

int         Name2Count(const std::string& arg);
void        clearCount();
std::string getStringFromInt(int in);
void        setNameMode(bool);

// Output Helper Functions.
std::ostream& outputHotSpotHeader(const char* filename);
void          outputHotSpotFooter(std::ostream& o);