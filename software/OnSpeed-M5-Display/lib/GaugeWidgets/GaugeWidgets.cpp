/*
Copyright 2020, 2021, 2022 V.R. Little

Permission is hereby granted, free of charge, to any person provided a copy of this software and associated documentation files
(the "Software") to use, copy, modify, or merge copies of the Software for non-commercial purposes, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS 
BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF 
OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

/*
  GaugeWidgets.cpp, the Engine Gauge Widget Library
    by V.R. ("Voltar") Little
    Version 2.0, April 2, 2020
    Version 2.1, July 13, 2020: 
      * Updated gradMarks function to support negative gauge values.  
      * Changed gradMarks colors from white to light grey.
      * Made BAR_LONG thinner for more precision.  
    Version 2.2, October 11, 2020:
      * Updated gradMarks function to support negative graduation marks to allow customization of major and minor marks.
          Any gradMarks > 1, then both major and minor graduation marks are automatically generated(backwards compatibility mode).
          If gradMarks < -1, then the setGradMarks method is used to specify major and minor graduation mark lengths and colors.
      * Added setLine methods.
      * Added setEdge methods.
      * Added setFill method.
      * Added setArc method.
      * Added drawLine methods.
      * Added drawTriangle methods.
      * Added drawQuadrangle methods.
      * Added drawRectangle methods.
      * Added drawArc methods.
      * General cleanup and minor bug fixes.
    Version 3.1, December 24, 2021:  
      * Added an INDEX pointer for arc/cicular gauges.  Similar to a needle, but a fixed width. VRL
      
  This library uses the huVVer-AVI hardware, but is easily ported to other ESP32 based devices that use TFT displays.
  It provides vertical,  horizontal, or arc (circular) widgets.

  The vertical and horizontal widgets use integer math for high speed drawing, but input pointer values may need
  to be scaled up to prevent integer rounding errors. Typically, multiplying single-digit pointer values by 
  10, 100 or 1000 will minize display errors.

  The arc widgets use mostly floating point math and will run slower.

   Widget Functions:
    <vBarGraph> defines the position and size of a vertical gauge.
    <hBarGraph> defines the position and size of a horizontal gauge.
    <arcGraph> defines the position, size and arc angles
      (both clockwise and counterclockwise)for circular or arc gauges.

   General Functions:
    <setPointer> defines the attributes of the discrete pointers and the pointer tags allowed per gauge.
    <clearPointers> clears all previously defined pointers variables.
    <setRange> defines the colored range bars for each gauge.
    <clearRanges> clears all previously defined ranges.
    <printNum> allows the printing of scalable, rotatable numbers and common arithmetical symbols.

   Library Variables:
    See GaugeWidgets_huVVer.h file for a complete listing of library variables.
*/

#include <Free_Fonts.h>
#include "GaugeWidgets.h"
#define GFXFF 1
//#define ARCSTEP 0.00872664626f     // Smaller steps for more arc accuracy, larger steps for faster execution for arc gauges.
//#define ARCSTEP 0.01745329252f   
//#define ARCSTEP 0.03496585039f   
//#define ARCSTEP 0.06981317008f
#define ARCSTEP 0.13962634016f   
#define TEXTSIZE FSSB12  // There are practical limits to font styles and sizes.  12 and 18 point sizes have been tested.
#define SCALEUP 4096     // upscaling integer math routines prevents significant rounding errors.

extern TFT_eSprite gdraw;

Gauges::Gauges() {}

//
// Draw cartesian lines
//

// Draw line 
void Gauges::drawLine (int16_t x0, int16_t y0, int16_t x1, int16_t y1, 
                       uint16_t lineColor, 
                       uint16_t lineWidth, uint8_t lineEnd,
                       uint16_t edgeColor, 
                       uint16_t edgeWidth, uint8_t edgeEnd){
                       
  fillLine (x0, y0, x1, y1, lineColor, lineWidth, lineEnd, edgeColor, edgeWidth, edgeEnd);
}

// Fill line 
void Gauges::fillLine (int16_t x0, int16_t y0, int16_t x1, int16_t y1, 
                       uint16_t lineColor, 
                       uint16_t lineWidth, uint8_t lineEnd,     
                       uint16_t edgeColor, 
                       uint16_t edgeWidth, uint8_t edgeEnd){
                       
  if (lineWidth == 1) gdraw.drawLine (x0, y0, x1, y1, lineColor);     
  
  else if (lineWidth > 1){
    float angle = atan2 (y1 - y0, x1 - x0);
    float sinA = (lineWidth / 2) * sin (angle);
    float cosA = (lineWidth / 2) * cos (angle);

    int16_t px3 = x0 + sinA; // - cosA; 
    int16_t py3 = y0 - cosA; // - sinA;
    int16_t px4 = x1 + sinA; // + cosA;
    int16_t py4 = y1 - cosA; // + sinA;
                      
    int16_t px5 = x0 - sinA; // - cosA;
    int16_t py5 = y0 + cosA; // - sinA;
    int16_t px6 = x1 - sinA; // + cosA;
    int16_t py6 = y1 + cosA; // + sinA;

    drawEdge (x0, y0, x1, y1, lineColor, lineWidth, lineEnd); 
    
	if (edgeWidth > 0) {
	  drawEdge (px3, py3, px4, py4, edgeColor, edgeWidth, edgeEnd);
	  drawEdge (px5, py5, px6, py6, edgeColor, edgeWidth, edgeEnd);    
	}
    
    if (lineEnd == NONE) {
	  drawEdge (px3, py3, px5, py5, edgeColor, edgeWidth, edgeEnd);
	  drawEdge (px4, py4, px6, py6, edgeColor, edgeWidth, edgeEnd); 
    }
    else if (lineEnd == SHARP) {
      int16_t px7 = x0 - cosA;
      int16_t py7 = y0 - sinA;
      int16_t px8 = x1 + cosA;
      int16_t py8 = y1 + sinA;

      // Triangle line ends
      gdraw.fillTriangle (px3, py3, px7, py7, px5, py5, lineColor);
      gdraw.fillTriangle (px4, py4, px8, py8, px6, py6, lineColor);
      
      if (edgeWidth > 0) {
       drawEdge (px3, py3, px7, py7, edgeColor, edgeWidth, edgeEnd);
       drawEdge (px7, py7, px5, py5, edgeColor, edgeWidth, edgeEnd);
       drawEdge (px4, py4, px8, py8, edgeColor, edgeWidth, edgeEnd);
       drawEdge (px8, py8, px6, py6, edgeColor, edgeWidth, edgeEnd);
      }
    }
    else if (lineEnd == ROUND) {
      //int16_t px7 = x0 - cosA;
      //int16_t py7 = y0 - sinA;
      //int16_t px8 = x1 + cosA;
      //int16_t py8 = y1 + sinA;
  
      if (lineWidth > 0) {
        gdraw.fillCircle (x0, y0, (lineWidth + edgeWidth) / 2 - 1, edgeColor);        
        gdraw.fillCircle (x1, y1, (lineWidth + edgeWidth) / 2 - 1, edgeColor);
      }
      
      gdraw.fillCircle (x0, y0, (lineWidth - edgeWidth) / 2 - 1, lineColor);
      gdraw.fillCircle (x1, y1, (lineWidth - edgeWidth) / 2 - 1, lineColor);
      drawEdge (x0, y0, x1, y1, lineColor, lineWidth - edgeWidth, NONE); //kludge to overwrite half circle*
    }
  }  
 } 

// Draw edge with all parameters
void Gauges::drawEdge (int16_t x0, int16_t y0, int16_t x1, int16_t y1, 
                       uint16_t edgeColor, uint16_t edgeWidth, uint8_t edgeEnd){  

  if (edgeWidth == 1) gdraw.drawLine (x0, y0, x1, y1, edgeColor);    
  
  else if (edgeWidth > 1){
    float angle = atan2 (y1 - y0, x1 - x0);
    float sinA = (edgeWidth / 2) * sin (angle);
    float cosA = (edgeWidth / 2) * cos (angle);

    int16_t px3 = x0 + sinA; // - cosA; 
    int16_t py3 = y0 - cosA; // - sinA;
    int16_t px4 = x1 + sinA; // + cosA;
    int16_t py4 = y1 - cosA; // + sinA;
                      
    int16_t px5 = x0 - sinA; // - cosA;
    int16_t py5 = y0 + cosA; // - sinA;
    int16_t px6 = x1 - sinA; // + cosA;
    int16_t py6 = y1 + cosA; // + sinA;

    gdraw.fillTriangle (px5, py5, px4, py4, px3, py3, edgeColor);         
    gdraw.fillTriangle (px4, py4, px5, py5, px6, py6, edgeColor);  
    
   if (edgeEnd == SHARP) {
      int16_t px7 = x0 - cosA;
      int16_t py7 = y0 - sinA;
      int16_t px8 = x1 + cosA;
      int16_t py8 = y1 + sinA;

      // Triangle line ends
      gdraw.fillTriangle (px3, py3, px7, py7, px5, py5, edgeColor);
      gdraw.fillTriangle (px4, py4, px8, py8, px6, py6, edgeColor);
    }
    else if (edgeEnd == ROUND) {
      gdraw.fillCircle (x0, y0, edgeWidth / 2, edgeColor);   
      gdraw.fillCircle (x1, y1, edgeWidth / 2, edgeColor);      
    }
  }  
}

// Draw triangle with edge color, width and end
void Gauges::drawTriangle (int16_t px1, int16_t py1, int16_t px2, int16_t py2, int16_t px3, int16_t py3,
                           uint16_t edgeColor, uint16_t edgeWidth, uint8_t edgeEnd){
  
  drawLine (px1, py1, px2, py2, edgeColor, edgeWidth, edgeEnd);
  drawLine (px2, py2, px3, py3, edgeColor, edgeWidth, edgeEnd);
  drawLine (px3, py3, px1, py1, edgeColor, edgeWidth, edgeEnd);
};

// Fill triangle with fill color, plus edge color, width and end
void Gauges::fillTriangle (int16_t px1, int16_t py1, int16_t px2, int16_t py2, int16_t px3, int16_t py3, 
                           uint16_t fillColor,
                           uint16_t edgeColor, uint16_t edgeWidth, uint8_t edgeEnd){

  if (fillColor != NOFILL){
    gdraw.fillTriangle (px1, py1, px2, py2, px3, py3, fillColor);
  }
                                
  drawLine (px1, py1, px2, py2, edgeColor, edgeWidth, edgeEnd);
  drawLine (px2, py2, px3, py3, edgeColor, edgeWidth, edgeEnd);
  drawLine (px3, py3, px1, py1, edgeColor, edgeWidth, edgeEnd);
};

// 
// Draw and fill cartesian quadrangles
// 

// Draw quadrangle
void Gauges::drawQuadrangle (int16_t px1, int16_t py1, int16_t px2, int16_t py2, 
                             int16_t px3, int16_t py3, int16_t px4, int16_t py4, 
                             uint16_t edgeColor, uint16_t edgeWidth, uint8_t edgeEnd){                             
  fillQuadrangle (px1, py1, px2, py2, px3, py3, px4, py4, NOFILL, edgeColor, edgeWidth, edgeEnd);    
};                         
                                    
// Fill quadrangle
void Gauges::fillQuadrangle (int16_t px1, int16_t py1, int16_t px2, int16_t py2, 
                             int16_t px3, int16_t py3, int16_t px4, int16_t py4, 
                             uint16_t fillColor, 
                             uint16_t edgeColor, uint16_t edgeWidth, uint8_t edgeEnd){
                                    
  // Draw constituant triangles  
  if (fillColor != NOFILL){ 
  
    gdraw.fillTriangle (px1, py1, px2, py2, px3, py3, fillColor);
    gdraw.fillTriangle (px3, py3, px4, py4, px1, py1, fillColor);
  }
  
  drawEdge (px1, py1, px2, py2, edgeColor, edgeWidth, edgeEnd);
  drawEdge (px2, py2, px3, py3, edgeColor, edgeWidth, edgeEnd);
  drawEdge (px3, py3, px4, py4, edgeColor, edgeWidth, edgeEnd);
  drawEdge (px4, py4, px1, py1, edgeColor, edgeWidth, edgeEnd);
  
 //// draw graduation marks if a rectangle is detected
 //if (gradMarks != 0 && px4 == px1 && px3 == px2 && py4 == py3 && py2 == py1){ //rectangles only
 //
 //  int16_t height = py4-py1;
 //  int16_t width = px2-px1;
 //  int16_t ax1;
 //  int16_t ay1;
 //  int16_t ax2;
 //  int16_t ay2;
 //  int16_t bx1;
 //  int16_t by1;
 //  int16_t bx2;
 //  int16_t by2;
 //  
 //  int16_t directionV = height / gradMarks;
 //  int16_t directionH = width / gradMarks;
 //
 //  if (abs(height) >= abs(width)){ // draw horizontal gradmarks if rectangle height >= width
 //  
 //      ax1 = (width - gradMajorLength) / 2; 
 //      ax2 = (width + gradMajorLength) / 2;  
 //       
 //    for (int16_t j = 1; j < gradMarks; j++) {
 //
 //      ay1 = ay2 = j * directionV;
 //      
 //      bx1 = px1 + ax1;
 //      by1 = py1 + ay1;
 //      bx2 = px1 + ax2;
 //      by2 = py1 + ay2;
 //      
 //      drawLine (bx1, by1, bx2, by2, gradMajorColor, gradMajorWidth, gradLineEnd, gdraw.alphaBlend(96, gradMajorColor, TFT_BLACK), 1);        
 //    }
 //
 //    ax1 = (width - gradMinorLength) / 2; 
 //    ax2 = (width + gradMinorLength) / 2;  
 //
 //    for (int16_t j = 0; j <= gradMarks-1; j++) {
 //
 //      ay1 = ay2 = j *  directionV + directionV/2;
 //
 //      bx1 = px1 + ax1;
 //      by1 = py1 + ay1;
 //      bx2 = px1 + ax2;
 //      by2 = py1 + ay2;
 //
 //      drawLine (bx1, by1, bx2, by2, gradMinorColor, gradMinorWidth, gradLineEnd, gdraw.alphaBlend(96, gradMinorColor, TFT_BLACK), 1);        
 //    }
 //  }
 //  // otherwise draw vertical gradmarks
 //  else {
 //    ay1 = (height - gradMajorLength) / 2;
 //    ay2 = (height + gradMajorLength) / 2;
 //      
 //    for (int16_t j = 1; j < gradMarks; j++) {
 //    
 //      ax1 = ax2 = j * directionH;
 //      
 //      bx1 = px1 + ax1;
 //      by1 = py1 + ay1;
 //      bx2 = px1 + ax2;
 //      by2 = py1 + ay2;
 //      
 //      drawLine (bx1, by1, bx2, by2, gradMajorColor, gradMajorWidth, gradLineEnd, gdraw.alphaBlend(96, gradMajorColor, TFT_BLACK), 1);      
 //    }
 //   
 //    ay1 = (height - gradMinorLength) / 2; 
 //    ay2 = (height + gradMinorLength) / 2;  
 //    
 //    for (int16_t j = 0 ; j <= gradMarks-1; j++) {
 //    
 //      ax1 = ax2 = j * directionH + directionH/2;
 //      
 //      bx1 = px1 + ax1;
 //      by1 = py1 + ay1;
 //      bx2 = px1 + ax2;
 //      by2 = py1 + ay2;
 //      
 //      drawLine (bx1, by1, bx2, by2, gradMinorColor, gradMinorWidth, gradLineEnd, gdraw.alphaBlend(96, gradMinorColor, TFT_BLACK), 1);      
 //    }
 //  }
 //}    
};

// 
// Draw and fill cartesian rectangles
// 
void Gauges::drawRectangle (int16_t px1, int16_t py1, int16_t width, int16_t height, 
                            uint16_t edgeColor, uint16_t edgeWidth, uint8_t edgeEnd){
  fillQuadrangle (px1, py1, px1 + width, py1, px1 + width, py1 + height, px1, py1 + height, NOFILL, edgeColor, edgeWidth, edgeEnd);                                
};

void Gauges::fillRectangle (int16_t px1, int16_t py1, int16_t width, int16_t height, 
                            uint16_t fillColor,
                            uint16_t edgeColor, uint16_t edgeWidth, uint8_t edgeEnd){
  fillQuadrangle (px1, py1, px1 + width, py1, px1 + width, py1 + height, px1, py1 + height, fillColor, edgeColor, edgeWidth, edgeEnd);                                
};

// 
// Draw graduation marks in a rectangle. Marks are always drawn parallel to the shorter edge.
// 
void Gauges::gradMarkRectangle (int16_t x0, int16_t y0, int16_t width, int16_t height){         // use predefined graduation mark parameters
gradMarkRectangle (x0, y0, width, height,
               gradMarks, gradMajorColor, gradMajorLength, gradMajorWidth,  
               gradMinorColor, gradMinorLength, gradMinorWidth, gradLineEnd);
}

void Gauges::gradMarkRectangle (int16_t x0, int16_t y0, int16_t width, int16_t height,         // use defined graduation mark parameters
                                int16_t gradMarks, 
                                uint16_t gradMajorColor, uint16_t gradMajorLength, uint16_t gradMajorWidth,  
                                uint16_t gradMinorColor, uint16_t gradMinorLength, uint16_t gradMinorWidth, 
                                uint8_t gradLineEnd){
                        
 if (gradMarks != 0){ 

    int16_t ax1;
    int16_t ay1;
    int16_t ax2;
    int16_t ay2;
    int16_t bx1;
    int16_t by1;
    int16_t bx2;
    int16_t by2;

    int16_t directionV = height / abs(gradMarks);
    int16_t directionH = width / abs(gradMarks);
 
    if (abs(height) >= abs(width)){ // draw horizontal gradmarks if rectangle height >= width
   
       ax1 = (width - gradMajorLength) / 2; 
       ax2 = (width + gradMajorLength) / 2;  
        
     for (int16_t j = 0; j <= abs(gradMarks); j++) {
 
       ay1 = ay2 = j * directionV;
       
       bx1 = x0 + ax1;
       by1 = y0 + ay1;
       bx2 = x0 + ax2;
       by2 = y0 + ay2;
       
       drawLine (bx1, by1, bx2, by2, gradMajorColor, gradMajorWidth, gradLineEnd, gdraw.alphaBlend(96, gradMajorColor, TFT_BLACK), 1);        
     }

      ax1 = (width - gradMinorLength) / 2; 
      ax2 = (width + gradMinorLength) / 2;  

      for (int16_t j = 0; j <abs(gradMarks); j++) {

       ay1 = ay2 = j *  directionV + directionV/2;

       bx1 = x0 + ax1;
       by1 = y0 + ay1;
       bx2 = x0 + ax2;
       by2 = y0 + ay2;

       drawLine (bx1, by1, bx2, by2, gradMinorColor, gradMinorWidth, gradLineEnd, gdraw.alphaBlend(96, gradMinorColor, TFT_BLACK), 1);        
      }
    }
    
   // otherwise draw vertical gradmarks
      else {
        ay1 = (height - gradMajorLength) / 2;
        ay2 = (height + gradMajorLength) / 2;
         
        for (int16_t j = 0; j <= abs(gradMarks); j++) {

         ax1 = ax2 = j * directionH;
         
         bx1 = x0 + ax1;
         by1 = y0 + ay1;
         bx2 = x0 + ax2;
         by2 = y0 + ay2;
         
         drawLine (bx1, by1, bx2, by2, gradMajorColor, gradMajorWidth, gradLineEnd, gdraw.alphaBlend(96, gradMajorColor, TFT_BLACK), 1);      
      }
    
      ay1 = (height - gradMinorLength) / 2; 
      ay2 = (height + gradMinorLength) / 2;  
     
      for (int16_t j = 0 ; j < abs(gradMarks); j++) {
     
       ax1 = ax2 = j * directionH + directionH/2;
       
       bx1 = x0 + ax1;
       by1 = y0 + ay1;
       bx2 = x0 + ax2;
       by2 = y0 + ay2;
       
       drawLine (bx1, by1, bx2, by2, gradMinorColor, gradMinorWidth, gradLineEnd, gdraw.alphaBlend(96, gradMinorColor, TFT_BLACK), 1);      
      }
    }
  }
} 

//
// Draw arcs and circles.  These are special instances of lines.
//
void Gauges::drawArc (int16_t x0, int16_t y0, int16_t radius,                               // use predefined graduation mark parameters
                      float startAngle, float arcAngle, 
                      uint16_t lineColor, uint16_t lineWidth, 
                      uint16_t edgeColor, uint16_t edgeWidth, 
                      uint8_t edgeEnd) {
  fillArc (x0, y0, radius, startAngle, arcAngle, lineColor, lineWidth, edgeColor, edgeWidth, edgeEnd);
}                        

void Gauges::fillArc (int16_t x0, int16_t y0, int16_t radius,                               // use local graduation mark parameters
                      float startAngle, float arcAngle, 
                      uint16_t lineColor, uint16_t lineWidth, 
                      uint16_t edgeColor, uint16_t edgeWidth, 
                      uint8_t edgeEnd) {

  float cosA;  
  float sinA;  
  float cosB;          
  float sinB;  
  
  int16_t x1 = 0, y1 = 0; 
  int16_t x2 = 0, y2 = 0; 
  int16_t x3 = 0, y3 = 0; 
  int16_t x4 = 0, y4 = 0; 
  int16_t nx1 = 0, ny1 = 0, nx2 = 0, ny2 = 0;
  
  for (float j = 0; j < abs(arcAngle); j += ARCSTEP) {
          
    float theta = startAngle + j;
    
    if (arcAngle >= 0) {
      cosA = cos(theta);
      sinA = sin(theta);
      cosB = cos(theta + ARCSTEP);
      sinB = sin(theta + ARCSTEP);
    }
    
    else { // counterClockwise
      cosA = -cos(theta);
      sinA =  sin(theta);
      cosB = -cos(theta - ARCSTEP);
      sinB =  sin(theta - ARCSTEP);
    }
     
    int16_t LW2 = lineWidth/2;
    x1 = x0 + (radius + LW2) * cosA;
    y1 = y0 + (radius + LW2) * sinA;
    x2 = x0 + (radius - LW2) * cosA;
    y2 = y0 + (radius - LW2) * sinA;
    x3 = x0 + (radius + LW2) * cosB;
    y3 = y0 + (radius + LW2) * sinB;
    x4 = x0 + (radius - LW2) * cosB;
    y4 = y0 + (radius - LW2) * sinB;
   
    if (lineColor != NOFILL){
      gdraw.fillTriangle(x1, y1, x2, y2, x3, y3, lineColor);
      gdraw.fillTriangle(x3, y3, x2, y2, x4, y4, lineColor);
    } 
    
    if (edgeWidth != 0) {
      drawEdge (x1, y1, x3, y3, edgeColor, edgeWidth, edgeEnd);
      drawEdge (x2, y2, x4, y4, edgeColor, edgeWidth, edgeEnd);
    }
    
    if (j == 0) {nx1 = x1; ny1 = y1; nx2 = x2; ny2 = y2;}
  }

  if (abs(arcAngle) < TWO_PI && edgeWidth != 0){
    drawEdge (nx1, ny1, nx2, ny2, edgeColor, edgeWidth, edgeEnd);
    drawEdge (x3, y3, x4, y4, edgeColor, edgeWidth, edgeEnd);
  }
}

// 
// Draw graduation marks in an arc. Marks are always drawn radially.
// 
void Gauges::gradMarkArc (int16_t x0, int16_t y0, int16_t radius, float startAngle, float arcAngle){    // use predefined graduation mark parameters
  gradMarkArc (x0, y0, radius, startAngle, arcAngle,
                gradMarks, gradMajorColor, gradMajorLength, gradMajorWidth,  
                gradMinorColor, gradMinorLength, gradMinorWidth, gradLineEnd);
}

void Gauges::gradMarkArc (int16_t x0, int16_t y0, int16_t radius, float startAngle, float arcAngle,     // use local graduation mark parameters
                          int16_t gradMarks, 
                          uint16_t gradMajorColor, uint16_t gradMajorLength, uint16_t gradMajorWidth,  
                          uint16_t gradMinorColor, uint16_t gradMinorLength, uint16_t gradMinorWidth, 
                          uint8_t gradLineEnd){

   float cosA;  
   float sinA;  
   //float cosB;          
   //float sinB;  
  
   if (gradMarks != 0){
     double gradStep = arcAngle/gradMarks;
     
     for (int16_t j = 0; j <= abs(gradMarks); j++) {
       cosA = cos (j * gradStep + startAngle);
       sinA = sin (j * gradStep + startAngle);
                         
       int16_t ax1 = x0 + (radius - 0.5f * gradMajorLength) * cosA;
       int16_t ay1 = y0 + (radius - 0.5f * gradMajorLength) * sinA;
       int16_t ax2 = x0 + (radius + 0.5f * gradMajorLength) * cosA;
       int16_t ay2 = y0 + (radius + 0.5f * gradMajorLength) * sinA;

       fillLine (ax1, ay1, ax2, ay2, gradMajorColor, gradMajorWidth, gradLineEnd, TFT_BLACK, 1);
     }

   for (int16_t j = 0; j < abs(gradMarks); j++) {

       cosA = cos(j * gradStep + startAngle + 0.5f * gradStep);
       sinA = sin(j * gradStep + startAngle + 0.5f * gradStep);
                         
       int16_t ax1 = x0 + (radius - 0.5f * gradMinorLength) * cosA;
       int16_t ay1 = y0 + (radius - 0.5f * gradMinorLength) * sinA;
       int16_t ax2 = x0 + (radius + 0.5f * gradMinorLength) * cosA;
       int16_t ay2 = y0 + (radius + 0.5f * gradMinorLength) * sinA;

     fillLine (ax1, ay1, ax2, ay2, gradMinorColor, gradMinorWidth, gradLineEnd, TFT_BLACK, 1);
   }
  }                       
}

//
// Gauge drawing methods
//

//
// Predefine parameters
//
void Gauges::setPointer (uint8_t Num, int16_t Value, uint8_t Type, uint16_t color, char Tag) {

  if (Num >= 1 && Num <= NUM_POINTERS) {
    pointerValue[Num] = Value;
    pointerType [Num] = Type;
    pointerColor [Num] = color;
    pointerTag [Num] = Tag;
  }
}

void Gauges::clearPointers(){
  for (int16_t i = 1; i <= NUM_POINTERS; i++) {
    pointerValue [i] = 0;
    pointerType [i] = 0;
    pointerColor [i] = 0;
    pointerTag [i] = '\0';
  }
}
  
void Gauges::setRange (uint8_t Num, bool Valid, int16_t Top, int16_t Bottom, uint16_t color) {

  if (Num >= 1 && Num <= NUM_RANGES) { 
    rangeValid[Num] = Valid;
    rangeTop[Num] = Top;
    rangeBot[Num] = Bottom;
    rangeColor[Num] = color;
  }
}

void Gauges::clearRanges(){
  for (int16_t i = 1; i <= NUM_RANGES; i++) {
    rangeValid[i] = false;
    rangeTop[i] = 0;
    rangeBot[i] = 0;
    rangeColor[i] = 0;
  }
}

void Gauges::setGradMarks (int16_t gMarks){
 gradMarks = gMarks;
 }

void Gauges::setGradMarks(uint16_t MjColor, uint16_t MjLen, uint16_t MjWidth,
                          uint16_t MnColor, uint16_t MnLen, uint16_t MnWidth, uint8_t gLineEnd){
  gradMajorColor =  MjColor;                    
  gradMajorLength = MjLen;
  gradMajorWidth = MjWidth;
  gradMinorColor =  MnColor;
  gradMinorLength = MnLen;  
  gradMinorWidth = MnWidth;
  gradLineEnd = gLineEnd;
}

void Gauges::clearGradMarks(){
  gradMarks = 0;
  gradMajorColor =  0;                    
  gradMajorLength = 0;
  gradMajorWidth = 0;
  gradMinorColor =  0;
  gradMinorLength = 0;  
  gradMinorWidth = 0;
  gradLineEnd = 0;
}

//
// Vertical bar graph gauge
//
void Gauges::vBarGraph (int16_t x0, int16_t y0, int16_t barSize, int16_t barWidth, int16_t maxDisplay, int16_t minDisplay, int16_t gradMarks) {

  int32_t _Width = barWidth;
  int32_t _pointerAdj[NUM_POINTERS + 1];
  int32_t _normAxis; 

  if ((maxDisplay - minDisplay) != 0) _normAxis  =  SCALEUP * barSize  / (maxDisplay - minDisplay) - 1;
  else _normAxis = 1; //don't ever want to divide by 0!
   
  for (int i = 1; i <= NUM_POINTERS; i++){
    _pointerAdj[i] = _normAxis * pointerValue[i] / SCALEUP;
  }
  
  int32_t _MaxDisplay = _normAxis * maxDisplay / SCALEUP;
  int32_t _MinDisplay = _normAxis * minDisplay / SCALEUP;
  int32_t _X0 = x0;
  int32_t _Y0 = y0;

  /*
     Normalize all of the gauge ranges.
   */
   
  _Y0 = _Y0 + _MinDisplay; // Offset widget to eliminate black space at bottom

  bool rangeisValid = false;
  
  /*
     Draw all of the enabled display bars.  Setting non-overlaping ranges allows for black bars between ranges.
   */  
  for (int16_t i = 1; i <= NUM_RANGES; i++){
    rangeTop[i] = rangeTop[i] * _normAxis / SCALEUP;
    rangeBot[i] = rangeBot[i] * _normAxis / SCALEUP;
    
    if (rangeTop[i] > _MaxDisplay) rangeTop[i] = _MaxDisplay;
    if (rangeBot[i] < _MinDisplay) rangeBot[i] = _MinDisplay;
    
    if (rangeValid[i]) {
	  gdraw.fillRect (_X0, _Y0 - rangeTop[i], _Width, rangeTop[i] - rangeBot[i], rangeColor[i]);
	  gdraw.drawRect (_X0, _Y0 - rangeTop[i], _Width, rangeTop[i] - rangeBot[i], TFT_BLACK);
      rangeisValid = true;
    }
  }
	
 // Draw a box around the gauge if any ranges enabled

  if (rangeisValid) gdraw.drawRect(_X0, _Y0 - _MaxDisplay, _Width,  _MaxDisplay - _MinDisplay, TFT_DARKGREY);

 // Draw gauge graduations

  if (gradMarks > 1) {	// Backwards compatability mode using predefined grad marks.
  
    int16_t delta = (_MaxDisplay - _MinDisplay) / gradMarks; 
    for (int16_t i = _MinDisplay; i <= _MaxDisplay; i += delta) { // major marks
      int16_t _X1 = _X0;
      int16_t _X2 = _X0 + _Width  - 1 ;
      int16_t _Y1 = _Y0 - i;

	  drawLine (_X1, _Y1, _X2, _Y1, TFT_LIGHTGREY, 4, NONE, gdraw.alphaBlend (96, TFT_BLACK, TFT_LIGHTGREY), 1, NONE);	 
    }

    for (int16_t i = _MinDisplay + delta / 2; i <= _MaxDisplay; i += delta) { // minor marks
      int16_t _X1 = _X0 + _Width / 4;
      int16_t _X2 = _X0 + 3 * _Width / 4 - 1;
      int16_t _Y1 = _Y0 - i;
      
      drawLine (_X1, _Y1, _X2, _Y1, TFT_LIGHTGREY, 4, NONE, gdraw.alphaBlend (96, TFT_BLACK, TFT_LIGHTGREY), 1, NONE);	
    }
  }

  if (gradMarks < -1) {	// GradMarks < -1 uses seperately defined major and minor gradmarks

	int16_t delta = (_MaxDisplay - _MinDisplay) / -gradMarks; 
	
    if (gradMajorLength != 0){
      for (int16_t i = _MinDisplay; i <= _MaxDisplay; i += delta) { // major marks
        int16_t _X1 = _X0 + (_Width  - gradMajorLength) / 2;
        int16_t _X2 = _X0 +  (_Width + gradMajorLength) / 2 ;
        int16_t _Y1 = _Y0 - i;
        
        drawLine (_X1, _Y1, _X2, _Y1, gradMajorColor, gradMajorWidth, gradLineEnd, TFT_DARKGREY, 1);	
      }
    }
    if (gradMinorLength != 0){ 
      for (int16_t i = _MinDisplay + delta / 2; i <= _MaxDisplay; i += delta) { // minor marks
        int16_t _X1 = _X0 + (_Width  - gradMinorLength) / 2;
        int16_t _X2 = _X0 +  (_Width + gradMinorLength) / 2 ;
        int16_t _Y1 = _Y0 - i;
        
        drawLine (_X1, _Y1, _X2, _Y1, gradMinorColor, gradMinorWidth, gradLineEnd, TFT_DARKGREY, 1);	
      } 
    }
  }

  // Pointer limits
  
  for (int16_t i = 1; i <= NUM_POINTERS; i++) {
  
    if (_pointerAdj[i] < _MinDisplay) _pointerAdj[i] = _MinDisplay;
    if (_pointerAdj[i] > _MaxDisplay) _pointerAdj[i] = _MaxDisplay;

    switch (pointerType[i]){
      case ARROW_LEFT: MarkArrowLeft(_X0, _Y0, _Width, _pointerAdj[i], pointerTag[i], pointerColor[i]); break;// arrow on left
      case ARROW_RIGHT: MarkArrowRight(_X0, _Y0, _Width, _pointerAdj[i], pointerTag[i], pointerColor[i]); break; // arrow on right
      case BAR_LONG: MarkHbar(_X0, _Y0, _Width, _pointerAdj[i], pointerTag[i], pointerColor[i]); break;      // long horizontal bar
      case BAR_SHORT: MarkHbarShort(_X0, _Y0, _Width, _pointerAdj[i], pointerTag[i], pointerColor[i]); break;     // short horizontal bar
      case BUG_LEFT: MarkBugLeft(_X0, _Y0, _Width, _pointerAdj[i], pointerTag[i], pointerColor[i]); break;    // bug on left  
      case BUG_RIGHT: MarkBugRight(_X0, _Y0, _Width, _pointerAdj[i], pointerTag[i], pointerColor[i]); break;   // bug on right 
      case ROUND_DOT: MarkHdot(_X0, _Y0, _Width, _pointerAdj[i], pointerTag[i], pointerColor[i]); break;   // round dot
      case 8: break; 
      default: break;
    } 
  }
 
// Text marker Top
   
  topDatumX = _X0 +  _Width/2; 
  topDatumY = _Y0 -  _MinDisplay - barSize;

// Text marker Bottom
 
  btmDatumX = topDatumX;
  btmDatumY = _Y0 - _MinDisplay; 
}

//
// Horizontal bar graph gauge
//
void Gauges::hBarGraph (int16_t x0, int16_t y0, int16_t barSize, int16_t barWidth, int16_t maxDisplay, int16_t minDisplay, int16_t gradMarks) {
 
  int32_t _Width = barWidth;
  int32_t _pointerAdj[NUM_POINTERS + 1];
  int32_t _normAxis; 

  if ((maxDisplay - minDisplay) != 0) _normAxis  =  SCALEUP * barSize  / (maxDisplay - minDisplay) - 1;
  else _normAxis = 1; //don't ever want to divide by 0!
   
  for (int i = 1; i <= NUM_POINTERS; i++){
    _pointerAdj[i] = _normAxis * pointerValue[i] / SCALEUP;
  }
  
  int32_t _MaxDisplay = _normAxis * maxDisplay / SCALEUP;
  int32_t _MinDisplay = _normAxis * minDisplay / SCALEUP;
  int32_t _X0 = x0;
  int32_t _Y0 = y0;

  // Normalize all of the gauge ranges.
   
  _X0 = _X0 - _MinDisplay; // Offset widget to eliminate black space at the left.

  bool rangeisValid = false;
  
  // Draw all of the enabled display bars.  Setting non-overlaping ranges allows for black bars between ranges.

  for (int16_t i = 1; i <= NUM_RANGES; i++){
    rangeTop[i] = rangeTop[i] * _normAxis / SCALEUP;
    rangeBot[i] = rangeBot[i] * _normAxis / SCALEUP;
    
    if (rangeTop[i] > _MaxDisplay) rangeTop[i] = _MaxDisplay;
    if (rangeBot[i] < _MinDisplay) rangeBot[i] = _MinDisplay;
    
    if (rangeValid[i]) {
      gdraw.fillRect (_X0 + rangeBot[i], _Y0, rangeTop[i] - rangeBot[i], _Width, rangeColor[i]);
      gdraw.drawRect (_X0 + rangeBot[i], _Y0, rangeTop[i] - rangeBot[i], _Width, TFT_BLACK);
      rangeisValid = true;
    }
  }

  if (rangeisValid) gdraw.drawRect (_X0 + _MinDisplay, _Y0, _MaxDisplay - _MinDisplay, _Width, TFT_DARKGREY);

  // Draw gauge graduations

  if (gradMarks > 1) {

    int16_t delta = (_MaxDisplay - _MinDisplay) / gradMarks;

    for (int16_t i = _MinDisplay; i < _MaxDisplay; i += delta ) { // major marks
      int16_t _Y1 = _Y0;
      int16_t _Y2 = _Y0 + _Width - 1;
      int16_t _X1 = _X0 + i;
      
      drawLine (_X1, _Y1, _X1, _Y2, TFT_LIGHTGREY, 4, NONE, gdraw.alphaBlend (96, TFT_BLACK, TFT_LIGHTGREY), 1);	
    }
    
    for (int16_t i = _MinDisplay + delta / 2; i < _MaxDisplay; i += delta ) { // minor marks
      int16_t _Y1 = _Y0 + _Width / 4;
      int16_t _Y2 = _Y0 + 3 * _Width / 4 - 1;
      int16_t _X1 = _X0 + i;
      
      drawLine (_X1, _Y1, _X1, _Y2, TFT_LIGHTGREY, 4, NONE, gdraw.alphaBlend (96, TFT_BLACK, TFT_LIGHTGREY), 1);	
    }
  }
 
  if (gradMarks < -1) {

	int16_t delta = (_MaxDisplay - _MinDisplay) / -gradMarks; 
    
    if (gradMajorLength != 0){
      for (int16_t i = _MinDisplay; i < _MaxDisplay; i += delta ) { // major marks
        int16_t _Y1 = _Y0 + (_Width - gradMajorLength) / 2;
        int16_t _Y2 = _Y0 + (_Width + gradMajorLength) / 2;
        int16_t _X1 = _X0 + i;
        
        drawLine (_X1, _Y1, _X1, _Y2, gradMajorColor, gradMajorWidth, NONE, TFT_DARKGREY, 1);	
      }      
    }
    if (gradMinorLength != 0){
      for (int16_t i = _MinDisplay + delta / 2; i < _MaxDisplay; i += delta ) { // minor marks
        int16_t _Y1 = _Y0 + (_Width - gradMinorLength) / 2;
        int16_t _Y2 = _Y0 + (_Width + gradMinorLength) / 2;
        int16_t _X1 = _X0 + i;
        
        drawLine (_X1, _Y1, _X1, _Y2, gradMinorColor, gradMinorWidth, NONE, TFT_DARKGREY, 1);	
      }
    }
  } 

   // Pointer limits

  for (int16_t i = 1; i <= NUM_POINTERS; i++) {
  
    if (_pointerAdj[i] < _MinDisplay) _pointerAdj[i] = _MinDisplay;
    if (_pointerAdj[i] > _MaxDisplay) _pointerAdj[i] = _MaxDisplay;

    switch (pointerType[i]){
      case ARROW_TOP: MarkArrowTop(_X0, _Y0, _Width, _pointerAdj[i], pointerTag[i], pointerColor[i]); break;// arrow on top
      case ARROW_BOTTOM: MarkArrowBottom(_X0, _Y0, _Width, _pointerAdj[i], pointerTag[i], pointerColor[i]); break; // arrow on bottom
      case BAR_LONG: MarkVbar(_X0, _Y0, _Width, _pointerAdj[i], pointerTag[i], pointerColor[i]); break;     // long horizontal bar
      case BAR_SHORT: MarkVbarShort(_X0, _Y0, _Width, _pointerAdj[i], pointerTag[i], pointerColor[i]); break;     // short horizontal bar
      case BUG_TOP: MarkBugTop(_X0, _Y0, _Width, _pointerAdj[i], pointerTag[i], pointerColor[i]); break;    // bug on top  
      case BUG_BOTTOM: MarkBugBot(_X0, _Y0, _Width, _pointerAdj[i], pointerTag[i], pointerColor[i]); break;   // bug on bottom 
      case ROUND_DOT: MarkVdot(_X0, _Y0, _Width, _pointerAdj[i], pointerTag[i], pointerColor[i]); break;   // round dot 
      case 8: break; 
      default: break;
    } 
  }

  // Text marker Right

  topDatumX = _X0 -  _MinDisplay + barSize;
  topDatumY = _Y0 + _Width/2; 
  
  // Text marker Left

  btmDatumX = _X0 + _MinDisplay; 
  btmDatumY = topDatumY; ;
}

/*
   Arc bar graph gauge, both clockwise and counterclockwise.
*/
void Gauges::arcGraph (int16_t x0, int16_t y0, int16_t barSize, int16_t barWidth, int16_t maxDisplay, int16_t minDisplay,
                       int16_t startAngle, int16_t arcAngle, bool clockWise, int16_t gradMarks) {

  // Define floating point variables derived from integer parameters                   

  float _normAxis;
  float _pointerAdj[NUM_POINTERS + 1];
  float _rangeTopAdj[NUM_RANGES + 1];
  float _rangeBotAdj[NUM_RANGES + 1];
  float _startAngle = startAngle * DEG_TO_RAD;
  float _arcAngle = abs(arcAngle) * DEG_TO_RAD; 
  
  if ((maxDisplay - minDisplay) != 0) _normAxis = _arcAngle / (maxDisplay - minDisplay);
  else _normAxis = 1.0f;
  
  float _maxDisplay = _normAxis * maxDisplay;
  float _minDisplay = _normAxis * minDisplay;  
  
  float _theta = _startAngle - _minDisplay; // for widget rotation
  //float _cosA, cosB, _sinA, _sinB;
  //int16_t _X1, _X2, _X3, _X4, _Y1, _Y2, _Y3, _Y4;
  
  // Scale all the pointers.
  
  for (int i = 1; i <= NUM_POINTERS; i++){
    _pointerAdj[i] = _normAxis * pointerValue[i];
  }

  // Normalize all of the gauge ranges.

  for (int16_t i = 1; i <= NUM_RANGES; i++){
    _rangeTopAdj[i] = rangeTop[i] * _normAxis; // SCALEUP;
    _rangeBotAdj[i] = rangeBot[i] * _normAxis; // SCALEUP;
    
    if (_rangeTopAdj[i] > _maxDisplay) _rangeTopAdj[i] = _maxDisplay;
    if (_rangeBotAdj[i] < _minDisplay) _rangeBotAdj[i] = _minDisplay;
 
	// Draw all of the enabled display sectors.  Setting non-overlaping ranges allows for blank bars between ranges.

	if (rangeValid[i]) {
      drawArc (x0,  y0,  barSize-barWidth/2, (_theta + _rangeBotAdj[i]), abs(_rangeTopAdj[i] - _rangeBotAdj[i]), 
      rangeColor[i],  barWidth,  gdraw.alphaBlend (96, TFT_BLACK, rangeColor[i]), 1);
	}
  }

  // Draw dial graduations
  
  if (gradMarks > 1) {
  
      float delta = _arcAngle / gradMarks;
	  
	for (float i = 0; i <= _arcAngle; i += delta) { // major marks
      float _angle = i + _startAngle;
	  float _cosA = cos (_angle);
	  float _sinA = sin (_angle);
	  float _cosB = -sin (_angle);
	  //float _sinB = cos (_angle);
	  
	  if (!clockWise) {
		_cosA = -_cosA;
		_cosB = -_cosB;
	  }
	  
	  float _X1 = x0 + (barSize - 1.25f * barWidth) * _cosA;
	  float _Y1 = y0 + (barSize - 1.25f * barWidth) * _sinA;
	  float _X2 = x0 + barSize * _cosA;
	  float _Y2 = y0 + barSize * _sinA;
      
	  drawLine (_X1, _Y1, _X2, _Y2, TFT_LIGHTGREY, 4, NONE, gdraw.alphaBlend (96, TFT_BLACK, TFT_LIGHTGREY), 1);
	}

	for (float i = _arcAngle / (gradMarks * 2); i < _arcAngle; i += delta) { // minor marks
      float _angle = i + _startAngle;
	  float _cosA = cos (_angle);
	  float _sinA = sin (_angle);
	  float _cosB = -sin (_angle);
	  //float _sinB = cos (_angle);
	  
	  if (!clockWise) {
		_cosA = -_cosA;
		_cosB = -_cosB;
	  }


	  float _X1 = x0 + (barSize - 0.75f * barWidth) * _cosA;
	  float _Y1 = y0 + (barSize - 0.75f * barWidth) * _sinA;
	  float _X2 = x0 + barSize * _cosA;
	  float _Y2 = y0 + barSize * _sinA;
      
      drawLine (_X1, _Y1, _X2, _Y2, TFT_LIGHTGREY, 4, NONE, gdraw.alphaBlend (96, TFT_BLACK, TFT_LIGHTGREY), 1);
	}  
  }
  
  if (gradMarks < -1) {
  
    float delta = _arcAngle / -gradMarks;
	
    if (gradMajorLength != 0){
    
      for (float i = 0; i <= _arcAngle; i += delta) { // major marks
        float _angle = i + _startAngle;
        float _cosA = cos (_angle);
        float _sinA = sin (_angle);
        float _cosB = -sin (_angle);
        //float _sinB = cos (_angle);
        
        if (!clockWise) {
          _cosA = -_cosA;
          _cosB = -_cosB;
        }
        
        float _X1 = x0 + (barSize - gradMajorLength) * _cosA;
        float _Y1 = y0 + (barSize - gradMajorLength) * _sinA;
        float _X2 = x0 + barSize * _cosA;
        float _Y2 = y0 + barSize * _sinA;
        
        drawLine (_X1, _Y1, _X2, _Y2, gradMajorColor, gradMajorWidth, NONE, TFT_DARKGREY, 1);	
      }
    }
   
    if (gradMinorLength != 0){
    
      for (float i = _arcAngle / (-gradMarks * 2); i < _arcAngle; i += delta) { // minor marks
        float _angle = i + _startAngle;
        float _cosA = cos (_angle);
        float _sinA = sin (_angle);
        float _cosB = -sin (_angle);
        //float _sinB = cos (_angle);
        
        if (!clockWise) {
          _cosA = -_cosA;
          _cosB = -_cosB;
        }
        
        int16_t _X1 = x0 + (barSize - gradMinorLength) * _cosA;
        int16_t _Y1 = y0 + (barSize - gradMinorLength) * _sinA;
        int16_t _X2 = x0 + barSize * _cosA;
        int16_t _Y2 = y0 + barSize * _sinA;
        
        drawLine (_X1, _Y1, _X2, _Y2, gradMinorColor, gradMinorWidth, NONE, TFT_DARKGREY, 1);	
      }
    }
  }

  // Text markers

  float midRadius = barSize-barWidth/2;
  
  if (clockWise){
	  topDatumX = x0 +  midRadius * cos (_startAngle + _arcAngle);  
	  topDatumY = y0 +  midRadius * sin (_startAngle + _arcAngle);  
                                                       
	  btmDatumX = x0 +  midRadius * cos (_startAngle);  
	  btmDatumY = y0 +  midRadius * sin (_startAngle);    
  }                                                  
  else {                                                 
  	  btmDatumX = x0 +  midRadius * cos (_startAngle + _arcAngle);  
	  btmDatumY = y0 +  midRadius * sin (_startAngle + _arcAngle);  
                                           
	  topDatumX = x0 +  midRadius * cos (_startAngle);    
	  topDatumY = y0 +  midRadius * sin (_startAngle);  
  } 
  
  // Pointer adjustments

  float _Angle = _theta;
          
  for (int16_t i = 1; i <= NUM_POINTERS; i++){
  
      if (_pointerAdj[i] < _minDisplay) _pointerAdj[i]  = _minDisplay;
      if (_pointerAdj[i] > _maxDisplay) _pointerAdj[i] = _maxDisplay;

      if (!clockWise) {
        _pointerAdj[i]  = PI - _pointerAdj[i];
        _Angle = -_theta;  
      }

    switch (pointerType[i]){
      case ARROW_OUT: MarkArrowOut(x0, y0, barSize, barWidth, _pointerAdj[i], pointerTag[i], _Angle, pointerColor[i]); break;
      case ARROW_IN: MarkArrowIn(x0, y0, barSize, barWidth, _pointerAdj[i], pointerTag[i], _Angle, pointerColor[i]); break;
      case BAR_LONG: MarkRbar(x0, y0, barSize, barWidth, _pointerAdj[i], pointerTag[i], _Angle, pointerColor[i]); break;
      case BAR_SHORT: MarkRbarShort(x0, y0, barSize, barWidth, _pointerAdj[i], pointerTag[i], _Angle, pointerColor[i]); break;
      case BUG_OUT: MarkBugOut(x0, y0, barSize, barWidth, _pointerAdj[i], pointerTag[i], _Angle, pointerColor[i]); break;
      case BUG_IN: MarkBugIn(x0, y0, barSize, barWidth, _pointerAdj[i], pointerTag[i], _Angle, pointerColor[i]); break;
      case NEEDLE: MarkNeedle(x0, y0, barSize, barWidth, _pointerAdj[i], pointerTag[i], _Angle, pointerColor[i]); break;
      case INDEX: MarkIndex(x0, y0, barSize, barWidth, _pointerAdj[i], pointerTag[i], _Angle, pointerColor[i]); break;
      case ROUND_DOT: MarkRdot(x0, y0, barSize, barWidth, _pointerAdj[i], pointerTag[i], _Angle, pointerColor[i]); break;
      default: break;
    }
   } 
 }

int16_t Gauges::printNum (String value, int16_t x0, int16_t y0, int16_t width, int16_t height, 
					   int16_t roll, uint16_t color, uint8_t datum, uint16_t lineWidth, uint8_t lineEnd){
/*
  Draw a fully scalable and rotable numeric string using segmented characters.
  This function only supports numeric digits, plus common mathematical symbols.
  Extensions for full alphanumerics is possible by adding segments, but execution will be slower.
*/

/*
    p3----p4 
    |     |
    |     |
    |     |
    p1-p7-p2
    |     |
    |     |
    |     |
    p5----p6
*/
	
  /*
    Establish a midbaseline segment
  */
  float rollRad = (float)roll * DEG_TO_RAD;	
  
  float cosRollRad = cos (rollRad);
  float sinRollRad = sin (rollRad);
  
  float halfHeight = (float)height * 0.5f;
  float halfWidth = (float)width * 0.5f;
 
  float xHalfHeight = halfHeight * sinRollRad;
  float yHalfHeight = halfHeight * cosRollRad;

  float xHalfWidth = halfWidth * cosRollRad;
  float yHalfWidth = halfWidth * sinRollRad;
 
  float px0, py0;
  
  int16_t arrayLength = value.length();

  switch (datum) {

    case TL_DATUM:
      px0 = x0 + xHalfHeight;
      py0 = y0 + yHalfHeight;
      break;

    case ML_DATUM:
      px0 = x0;
      py0 = y0;
      break;

    case BL_DATUM:
      px0 = x0 - xHalfHeight;
      py0 = y0 - yHalfHeight;
      break;

    case TC_DATUM:
      px0 = x0 - (arrayLength - 1) * 1.5f * xHalfWidth;
      py0 = y0 + (arrayLength - 1) * 1.5f * yHalfWidth;
      px0 = px0 + xHalfHeight;
      py0 = py0 + yHalfHeight;
      break;

    case MC_DATUM:
      px0 = x0 - (arrayLength - 1) * 1.5f * xHalfWidth;
      py0 = y0 + (arrayLength - 1) * 1.5f * yHalfWidth;
      break;

    case BC_DATUM:
      px0 = x0 - (arrayLength - 1) * 1.5f * xHalfWidth;
      py0 = y0 + (arrayLength - 1) * 1.5f * yHalfWidth;
      px0 = px0 - xHalfHeight;
      py0 = py0 - yHalfHeight;
      break;

    case TR_DATUM:
      px0 = x0 - (arrayLength - 1) * 3.0f * xHalfWidth;
      py0 = y0 + (arrayLength - 1) * 3.0f * yHalfWidth;
      px0 = px0 + xHalfHeight;
      py0 = py0 + yHalfHeight;
      break;

    case MR_DATUM:
      px0 = x0 - (arrayLength - 1) * 3.0f * xHalfWidth;
      py0 = y0 + (arrayLength - 1) * 3.0f * yHalfWidth;
      break;

    case BR_DATUM:
      px0 = x0 - (arrayLength - 1) * 3.0f * xHalfWidth;
      py0 = y0 + (arrayLength - 1) * 3.0f * yHalfWidth;
      px0 = px0 - xHalfHeight;
      py0 = py0 - yHalfHeight;
      break;

    default:
      px0 = x0;
      py0 = y0;
      break;
  }

  for (uint16_t i = 0; i < arrayLength; i++){
  
    char numChar = value.charAt(i);
  
    // Adjust it for roll and pitch
    
      float px1 = px0 - xHalfWidth;
      float py1 = py0 + yHalfWidth;
      
      float px2 = px0 + xHalfWidth;
      float py2 = py0 - yHalfWidth;

      float px7 = px0;
      float py7 = py0;

     // Compute offset parallel line segments
     
     // xHalfHeight is sin(rollRad) * halfHeight;
     // yHalfHeight is cos(rollRad) * halfHeight;
    
      float px3 = px1 - xHalfHeight;    //cos(roll * DEG_TO_RAD - HALF_PI);
      float py3 = py1 - yHalfHeight;    //sin(-roll * DEG_TO_RAD - HALF_PI);
                        
      float px4 = px2 - xHalfHeight;    //cos(rollRad + HALF_PI);
      float py4 = py2 - yHalfHeight;    //sin(roll * DEG_TO_RAD + HALF_PI);
                       
      float px5 = px1 + xHalfHeight;    //cos(roll * DEG_TO_RAD - HALF_PI);
      float py5 = py1 + yHalfHeight;    //sin(-roll * DEG_TO_RAD - HALF_PI);
                       
      float px6 = px2 + xHalfHeight;    //cos(rollRad + HALF_PI);
      float py6 = py2 + yHalfHeight;    //sin(roll * DEG_TO_RAD + HALF_PI);
	  
  switch (numChar){

    case '0':
      drawLine (px3, py3, px4, py4, color, lineWidth, lineEnd);
      drawLine (px5, py5, px6, py6, color, lineWidth, lineEnd);                                   
      drawLine (px3, py3, px5, py5, color, lineWidth, lineEnd);
      drawLine (px4, py4, px6, py6, color, lineWidth, lineEnd);
      drawLine (px4, py4, px5, py5, color, lineWidth, lineEnd);
    break;
    
    case '1':
      drawLine (px2, py2, px4, py4, color, lineWidth, lineEnd);
      drawLine (px2, py2, px6, py6, color, lineWidth, lineEnd);
    break;
     
    case '2':
      drawLine (px1, py1, px2, py2, color, lineWidth, lineEnd);
      drawLine (px3, py3, px4, py4, color, lineWidth, lineEnd);
      drawLine (px5, py5, px6, py6, color, lineWidth, lineEnd);                                   

      drawLine (px1, py1, px5, py5, color, lineWidth, lineEnd);
      drawLine (px2, py2, px4, py4, color, lineWidth, lineEnd);
    break;
     
    case '3':
      drawLine (px1, py1, px2, py2, color, lineWidth, lineEnd);
      drawLine (px3, py3, px4, py4, color, lineWidth, lineEnd);
      drawLine (px5, py5, px6, py6, color, lineWidth, lineEnd);                                   
      drawLine (px2, py2, px4, py4, color, lineWidth, lineEnd);
      drawLine (px2, py2, px6, py6, color, lineWidth, lineEnd);
    break;
     
    case '4':
      drawLine (px1, py1, px2, py2, color, lineWidth, lineEnd);
      drawLine (px1, py1, px4, py4, color, lineWidth, lineEnd);
      drawLine (px2, py2, px4, py4, color, lineWidth, lineEnd);
      drawLine (px2, py2, px6, py6, color, lineWidth, lineEnd);
    break;
    
    case '5':
      drawLine (px4, py4, px3, py3, color, lineWidth, lineEnd);
      drawLine (px3, py3, px1, py1, color, lineWidth, lineEnd);
      drawLine (px1, py1, px2, py2, color, lineWidth, lineEnd);
      drawLine (px2, py2, px6, py6, color, lineWidth, lineEnd);         
      drawLine (px6, py6, px5, py5, color, lineWidth, lineEnd);
                    
    break; 
    
    case '6':
      drawLine (px1, py1, px2, py2, color, lineWidth, lineEnd);
      drawLine (px5, py5, px6, py6, color, lineWidth, lineEnd);                                   
      drawLine (px1, py1, px5, py5, color, lineWidth, lineEnd);
      drawLine (px2, py2, px6, py6, color, lineWidth, lineEnd);
      drawLine (px1, py1, px4, py4, color, lineWidth, lineEnd);    
    break;
    
    case '7':
      drawLine (px3, py3, px4, py4, color, lineWidth, lineEnd);
      drawLine (px4, py4, px5, py5, color, lineWidth, lineEnd);
    break;
    
    case '8':
      drawLine (px1, py1, px2, py2, color, lineWidth, lineEnd);
      drawLine (px3, py3, px4, py4, color, lineWidth, lineEnd);
      drawLine (px5, py5, px6, py6, color, lineWidth, lineEnd);                                   
      drawLine (px3, py3, px5, py5, color, lineWidth, lineEnd);
      drawLine (px4, py4, px6, py6, color, lineWidth, lineEnd);
    break;
      
    case '9':
      drawLine (px1, py1, px2, py2, color, lineWidth, lineEnd);
      drawLine (px3, py3, px4, py4, color, lineWidth, lineEnd); 
      drawLine (px2, py2, px4, py4, color, lineWidth, lineEnd);          
      drawLine (px1, py1, px3, py3, color, lineWidth, lineEnd);
      drawLine (px2, py2, px5, py5, color, lineWidth, lineEnd);
    break;
      
    case '-':
      drawLine (px1, py1, px2, py2, color, lineWidth, lineEnd);
    break;  
    
    case '.':
      gdraw.drawCircle(px6, py6, 0.50f * halfWidth, color);
    break;
    
    case 'o':
      gdraw.drawCircle (px3, py3, 0.50f * halfWidth, color);
    break; 
    
    case '%':
      drawLine (px4, py4, px5, py5, color, lineWidth, lineEnd);
      gdraw.drawCircle (px3, py3, 0.50f * halfWidth, color);
      gdraw.drawCircle (px6, py6, 0.50f * halfWidth, color);
    break;
    
    case ':':
      gdraw.drawCircle (px3, py3, 0.50f * halfWidth, color);
      gdraw.drawCircle (px5, py5, 0.50f * halfWidth, color);
    break;
    
    case '[':
      drawLine (px3, py3, px4, py4, color, lineWidth, lineEnd);
      drawLine (px5, py5, px6, py6, color, lineWidth, lineEnd);                                   
      drawLine (px3, py3, px5, py5, color, lineWidth, lineEnd);
    break;
      
    case ']':
      drawLine (px3, py3, px4, py4, color, lineWidth, lineEnd);
      drawLine (px4, py4, px6, py6, color, lineWidth, lineEnd);                                   
      drawLine (px5, py5, px6, py6, color, lineWidth, lineEnd);
    break;
    
    case '\\':
      drawLine (px3, py3, px6, py6, color, lineWidth, lineEnd);
    break;

    case '/':
      drawLine (px4, py4, px5, py5, color, lineWidth, lineEnd);
    break;
    
    case '*':
      drawLine (px1, py1, px2, py2, color, lineWidth, lineEnd);
      drawLine (px3, py3, px6, py6, color, lineWidth, lineEnd);
      drawLine (px4, py4, px5, py5, color, lineWidth, lineEnd);
    break;
      
    case '=':
      drawLine (px1, py1, px2, py2, color, lineWidth, lineEnd);
      drawLine (px5, py5, px6, py6, color, lineWidth, lineEnd);
    break;
    
    case '>':
      drawLine (px3, py3, px2, py2, color, lineWidth, lineEnd);
      drawLine (px2, py2, px5, py5, color, lineWidth, lineEnd);
    break;
      
    case '<':
      drawLine (px1, py1, px4, py4, color, lineWidth, lineEnd);
      drawLine (px1, py1, px6, py6, color, lineWidth, lineEnd);
    break;
  
    case '$':
      drawLine (px1, py1, px2, py2, color, lineWidth, lineEnd);
      drawLine (px3, py3, px4, py4, color, lineWidth, lineEnd);
      drawLine (px5, py5, px6, py6, color, lineWidth, lineEnd);                                   
      drawLine (px1, py1, px3, py3, color, lineWidth, lineEnd);
      drawLine (px2, py2, px6, py6, color, lineWidth, lineEnd);
      drawLine (px4, py4, px5, py5, color, lineWidth, lineEnd);      
    break;
    
    case '+':
      drawLine (px1, py1, px2, py2, color, lineWidth, lineEnd);
      drawLine (px5, py5, px4, py4, color, lineWidth, lineEnd);   
    break;
    
    case '^':
      drawLine (px1, py1, px4, py4, color, lineWidth, lineEnd);
      drawLine (px2, py2, px4, py4, color, lineWidth, lineEnd);   
    break;
    
    case '(':
      drawLine (px4, py4, px7, py7, color, lineWidth, lineEnd);
      drawLine (px7, py7, px6, py6, color, lineWidth, lineEnd);   
    break;
    
    case ')':
      drawLine (px3, py3, px7, py7, color, lineWidth, lineEnd);
      drawLine (px7, py7, px5, py5, color, lineWidth, lineEnd);   
    break;
         
    case '&':
      drawLine (px4, py4, px1, py1, color, lineWidth, lineEnd);
      drawLine (px1, py1, px2, py2, color, lineWidth, lineEnd);
      drawLine (px1, py1, px6, py6, color, lineWidth, lineEnd);                                   
    break;
    
    case '\n': 
      px0 -= 3.0f * halfWidth * (i + 1);
      py0 += 3.0f * halfHeight;
    break;

    default: 
      break;
    }

   px0 += 3.0f * xHalfWidth;  //step to next character position
   py0 -= 3.0f * yHalfWidth;  //with a half space between (3 = 3/2)
  }
  return arrayLength * 3 * width / 2;
}

//
// *******Private Functions******
// Arrows, bars, bugs and needle widgets
//

void Gauges::MarkArrowLeft(int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color) {

  fillTriangle (x0 + barWidth / 2, y0 - pointer, x0 - barWidth / 2, y0 - pointer - barWidth / 3, x0 - barWidth / 2, 
                y0 - pointer + barWidth / 3, color, TFT_BLACK, 2, SHARP);

  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 - barWidth - 1, y0 - pointer - 1, GFXFF);
  gdraw.drawString ((String)tag, x0 - barWidth + 1, y0 - pointer + 1, GFXFF);
  gdraw.setTextColor (color);
  gdraw.drawString ((String)tag, x0 - barWidth, y0 - pointer, GFXFF);
}

void Gauges::MarkArrowRight(int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color) {

  fillTriangle (x0 + barWidth / 2, y0 - pointer, x0 + 3 * barWidth / 2, y0 - pointer - barWidth / 3, x0 + 3 * barWidth / 2, 
                y0 - pointer + barWidth / 3, color, TFT_BLACK, 2, SHARP);

  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 + 2 * barWidth - 1, y0 - pointer - 1, GFXFF);
  gdraw.drawString ((String)tag, x0 + 2 * barWidth + 1, y0 - pointer + 1, GFXFF);
  gdraw.setTextColor (color);
  gdraw.drawString ((String)tag, x0 + 2 * barWidth, y0 - pointer, GFXFF);
}

void Gauges::MarkBugLeft(int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color) {

 /*
   Left bug
      
  p3------p1      `       
   |      | 
  p8\     |
     \    |        
  p6 p7  p5 
     /    |  
  p9/     |
   |      |
  p4------p2             
    
  */
  
  int16_t x5 = x0;
  int16_t y5 = y0 - pointer;
  
  int16_t x6 = x5 - barWidth / 2;
  int16_t y6 = y5;
  int16_t x1 = x5;
  int16_t y1 = y5 - barWidth / 2;
  int16_t x2 = x5;
  int16_t y2 = y5 + barWidth / 2;
  int16_t x3 = x6;
  int16_t y3 = y1;
  int16_t x4 = x6;
  int16_t y4 = y2;
  int16_t x7 = x5 - barWidth / 4 ;
  int16_t y7 = y5;
  int16_t x8 = x6;
  int16_t y8 = y6 - barWidth / 4;
  int16_t x9 = x6;
  int16_t y9 = y6 + barWidth / 4;
  
  gdraw.fillTriangle (x1, y1, x3, y3, x4, y4, color);
  gdraw.fillTriangle (x2, y2, x4, y4, x1, y1,  color);
  gdraw.fillTriangle (x8, y8, x9, y9, x7, y7, TFT_BLACK);
  drawLine (x1, y1, x2, y2, TFT_BLACK, 1, NONE);
  
  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 - barWidth - 1, y0 - pointer - 1, GFXFF);
  gdraw.drawString ((String)tag, x0 - barWidth + 1, y0 - pointer + 1, GFXFF);
  gdraw.setTextColor (color);
  gdraw.drawString ((String)tag, x0 - barWidth, y0 - pointer, GFXFF);
}

void Gauges::MarkBugRight(int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color) {

 /*
   Right bug
      
  p1------p3
   |      |
   |     /p8
   |    /   
  p5   p7 p6
   |    \
   |     \p9  
   |      |
  p2------p4

  */
  
  int16_t x5 = x0 + barWidth;
  int16_t y5 = y0 - pointer;
  int16_t x6 = x5 + barWidth / 2;
  int16_t y6 = y5;
  int16_t x1 = x5;
  int16_t y1 = y5 - barWidth / 2;
  int16_t x2 = x5;
  int16_t y2 = y5 + barWidth / 2;
  int16_t x3 = x6;
  int16_t y3 = y1;
  int16_t x4 = x6;
  int16_t y4 = y2;
  int16_t x7 = x5 + barWidth / 4;
  int16_t y7 = y5;
  int16_t x8 = x6;
  int16_t y8 = y6 - barWidth / 4;
  int16_t x9 = x6;
  int16_t y9 = y6 + barWidth / 4;
  
  gdraw.fillTriangle (x1, y1, x3, y3, x4, y4, color);
  gdraw.fillTriangle (x2, y2, x4, y4, x1, y1,  color);
  gdraw.fillTriangle (x8, y8, x9, y9, x7, y7, TFT_BLACK);
  drawLine (x1, y1, x2, y2, TFT_BLACK, 1, NONE);
	
  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 + 2 * barWidth - 1, y0 - pointer - 1, GFXFF);
  gdraw.drawString ((String)tag, x0 + 2 * barWidth + 1, y0 - pointer + 1, GFXFF);
  gdraw.setTextColor (color);
  gdraw.drawString ((String)tag, x0 + 2 * barWidth, y0 - pointer, GFXFF);
}

void Gauges::MarkHbar(int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color) {

  /* 
    Long Horizontal Bar

    ---------
   |         |
    ---------

  */

  gdraw.fillRect (x0 - barWidth / 4, y0 - pointer - barWidth / 8, 3 * barWidth / 2, barWidth / 4, color);
  gdraw.drawRect (x0 - barWidth / 4, y0 - pointer - barWidth / 8, 3 * barWidth / 2, barWidth / 4, TFT_BLACK);

  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_WHITE);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 + barWidth / 2 - 1, y0 - pointer - 1, GFXFF);
  gdraw.drawString ((String)tag, x0 + barWidth / 2 + 1, y0 - pointer + 1, GFXFF);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.drawString ((String)tag, x0 + barWidth / 2 , y0 - pointer, GFXFF);
}

void Gauges::MarkHbarShort(int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color) {

  /* 
    Short Horizontal Bar

    ---------
   |         |
    ---------

  */

  gdraw.fillRect (x0 + barWidth / 8, y0 - pointer - barWidth / 8, 3 * barWidth / 4, barWidth / 4, color);
  gdraw.drawRect (x0 + barWidth / 8, y0 - pointer - barWidth / 8, 3 * barWidth / 4, barWidth / 4, TFT_BLACK);

  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_WHITE);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 + barWidth / 2 - 1, y0 - pointer - 1, GFXFF);
  gdraw.drawString ((String)tag, x0 + barWidth / 2 + 1, y0 - pointer + 1, GFXFF);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.drawString ((String)tag, x0 + barWidth / 2 , y0 - pointer, GFXFF);
}

void Gauges::MarkHdot(int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color) {
 
/*
  This is for Vertical bar gauges, despite the name.
*/
  gdraw.fillCircle (x0 + barWidth / 2, y0 - pointer, barWidth / 5, color);
  gdraw.drawCircle (x0 + barWidth / 2, y0 - pointer, barWidth / 5, TFT_BLACK);

  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 + barWidth / 2, y0 - pointer, GFXFF);
}

void Gauges::MarkArrowTop(int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color) {

  /* 
    Top arrow

    .------.
     \    /
      \  /
       \/        
                 
  */
  
  fillTriangle (x0 + pointer, y0 + barWidth / 2, x0 + pointer - barWidth / 3, y0 - barWidth / 2,
                x0 + pointer + barWidth / 3, y0 - barWidth / 2, color,  TFT_BLACK, 2, SHARP);

  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 + pointer - 1, y0 - barWidth - 1, GFXFF);
  gdraw.drawString ((String)tag, x0 + pointer + 1 , y0 - barWidth + 1, GFXFF);
  gdraw.setTextColor(color);
  gdraw.drawString ((String)tag, x0 + pointer, y0 - barWidth, GFXFF);
}

void Gauges::MarkArrowBottom(int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color) {


  /* 
    Bottom Arrow

       /\
      /  \
     /    \
    '------'
        
  */

  fillTriangle (x0 + pointer, y0 + barWidth / 2, x0 + pointer - barWidth / 3, y0 + 3 * barWidth / 2,
                x0 + pointer + barWidth / 3, y0 + 3 * barWidth / 2, color, TFT_BLACK, 2, SHARP);

  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 + pointer - 1, y0 + 2 * barWidth - 1, GFXFF);
  gdraw.drawString ((String)tag, x0 + pointer + 1 , y0 + 2 * barWidth + 1, GFXFF);
  gdraw.setTextColor(color);
  gdraw.drawString ((String)tag, x0 + pointer, y0 + 2 * barWidth, GFXFF);
}

void Gauges::MarkBugTop(int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color) {

  /*
  
	Top bug
	  
    p3_p8  p6  p9_p4
    |   \     /   |
    |    \   /    | 
    |     \ /     |
    |      p7     |
    |             |
    p1_____p5_____p2   

  */
  
  int16_t x5 = x0 + pointer;
  int16_t y5 = y0;  
  int16_t x6 = x5;
  int16_t y6 = y5 - barWidth / 2;    
  int16_t x1 = x5 - barWidth / 2;
  int16_t y1 = y5;
  int16_t x2 = x5 + barWidth / 2;
  int16_t y2 = y5;
  int16_t x3 = x6 - barWidth / 2;
  int16_t y3 = y6;  
  int16_t x4 = x6 + barWidth / 2;
  int16_t y4 = y6;  
  int16_t x7 = x5;
  int16_t y7 = y5 - barWidth / 4;
  int16_t x8 = x6 - barWidth / 4;
  int16_t y8 = y6;
  int16_t x9 = x6 + barWidth / 4;
  int16_t y9 = y6;
  
  gdraw.fillTriangle (x1, y1, x3, y3, x4, y4, color);
  gdraw.fillTriangle (x2, y2, x4, y4, x1, y1,  color);
  gdraw.fillTriangle (x8, y8, x9, y9, x7, y7, TFT_BLACK);
  drawLine (x1, y1, x2, y2, TFT_BLACK, 1, NONE);

  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 + pointer - 1, y0 - barWidth - 1, GFXFF);
  gdraw.drawString ((String)tag, x0 + pointer + 1 , y0 - barWidth + 1, GFXFF);
  gdraw.setTextColor(color);
  gdraw.drawString ((String)tag, x0 + pointer, y0 - barWidth, GFXFF);
}

void Gauges::MarkBugBot(int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color) {

 /*
   Bottom bug

  p1_____p5_____p2
  |             |
  |      p7     |
  |      /\     |
  |     /  \    |
  |    /    \   |
  p3_p8  p6  p9_p4
    
  */

  int16_t x5 = x0 + pointer;
  int16_t y5 = y0 + barWidth;  
  int16_t x6 = x5;
  int16_t y6 = y5 + barWidth / 2;
  int16_t x1 = x5 - barWidth / 2;
  int16_t y1 = y5;  
  int16_t x2 = x5 + barWidth / 2;
  int16_t y2 = y5;  
  int16_t x3 = x6 - barWidth / 2;
  int16_t y3 = y6;  
  int16_t x4 = x6 + barWidth / 2;
  int16_t y4 = y6;  
  int16_t x7 = x5;
  int16_t y7 = y5 + barWidth / 4;
  int16_t x8 = x6 - barWidth / 4;
  int16_t y8 = y6;
  int16_t x9 = x6 + barWidth / 4;
  int16_t y9 = y6;
  
  gdraw.fillTriangle (x1, y1, x3, y3, x4, y4, color);
  gdraw.fillTriangle (x2, y2, x4, y4, x1, y1,  color);
  gdraw.fillTriangle (x8, y8, x9, y9, x7, y7, TFT_BLACK);
  drawLine (x1, y1, x2, y2, TFT_BLACK, 1, NONE);

  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 + pointer - 1, y0 + 2 * barWidth - 1, GFXFF);
  gdraw.drawString ((String)tag, x0 + pointer + 1 , y0 + 2 * barWidth + 1, GFXFF);
  gdraw.setTextColor(color);
  gdraw.drawString ((String)tag, x0 + pointer, y0 + 2 * barWidth, GFXFF);

}

void Gauges::MarkVbar(int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color) {

  /* 
    Long Vertical Bar
    
    --
   |  | 
   |  | 
   |  | 
   |  |  
    --
    
    */

  gdraw.fillRect(x0 + pointer - barWidth / 8, y0 - barWidth / 4, barWidth / 4, 3 * barWidth / 2, color);
  gdraw.drawRect (x0 + pointer - barWidth / 8, y0 - barWidth / 4, barWidth / 4, 3 * barWidth / 2, TFT_BLACK);

  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_WHITE);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 + pointer - 1, y0 + barWidth / 2 - 1, GFXFF);
  gdraw.drawString ((String)tag, x0 + pointer + 1, y0 + barWidth / 2 + 1, GFXFF);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.drawString ((String)tag, x0 + pointer, y0 + barWidth / 2, GFXFF);
}

void Gauges::MarkVbarShort(int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color) {


  /* 
    Short Vertical Bar
    
    --
   |  | 
   |  | 
   |  | 
   |  |  
    --
    
    */

  gdraw.fillRect(x0 + pointer - barWidth / 8, y0 + barWidth / 8, barWidth / 4, 3 * barWidth / 4, color);
  gdraw.drawRect (x0 + pointer - barWidth / 8, y0 + barWidth / 8, barWidth / 4, 3 * barWidth / 4, TFT_BLACK);

  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_WHITE);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 + pointer - 1, y0 + barWidth / 2 - 1, GFXFF);
  gdraw.drawString ((String)tag, x0 + pointer + 1, y0 + barWidth / 2 + 1, GFXFF);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.drawString ((String)tag, x0 + pointer, y0 + barWidth / 2, GFXFF);
}

void Gauges::MarkVdot(int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color) {
 
 // This is for horizontal bar gauges, despite the name.

  gdraw.fillCircle (x0 + pointer, y0 + barWidth / 2 , barWidth / 5, color);
  gdraw.drawCircle (x0 + pointer, y0 + barWidth / 2 , barWidth / 5, TFT_BLACK);

  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 + pointer, y0 + barWidth / 2, GFXFF);
}

void Gauges::MarkArrowOut(float x0, float y0, float barSize, float barWidth, float pointer, char tag, float theta, uint16_t color) {

  /* 
    Outside arrow (pointing in)
  
    p2-p1-p3
     \    /
      \  /       
       \/        
       p4        

  */
        
  float _Pointer = (pointer + theta);
  //float _Factor = barSize / (barSize - 1.5f * barWidth);
  float cosA = cos (_Pointer);
  float sinA = sin (_Pointer);
  float bw1 = barSize + 0.50f * barWidth;
  float bw2 = barSize - 0.50f * barWidth;
  float bw3 = 0.33f * barWidth;
  
  float x1 = x0 + bw1 * cosA;
  float y1 = y0 + bw1 * sinA;
  float x4 = x0 + bw2 * cosA;
  float y4 = y0 + bw2 * sinA;  
  float x2 = x1 + bw3 * sinA;  
  float y2 = y1 + bw3 * -cosA;
  float x3 = x1 + bw3 * -sinA;
  float y3 = y1 + bw3 * cosA;
  
  gdraw.fillTriangle (x4, y4, x3, y3, x2, y2, color);
  gdraw.drawTriangle (x4, y4, x3, y3, x2, y2, TFT_BLACK);
  
  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 + (int16_t)((barSize + barWidth) * cos(_Pointer)) - 1,
                    y0 + (int16_t)((barSize + barWidth)*sin(_Pointer)) - 1, GFXFF);
  gdraw.drawString ((String)tag, x0 + (int16_t)((barSize + barWidth) * cos(_Pointer)) + 1,
                    y0 + (int16_t)((barSize + barWidth)*sin(_Pointer)) + 1, GFXFF);
  gdraw.setTextColor (color);
  gdraw.drawString ((String)tag, x0 + (int16_t)((barSize + barWidth) * cos(_Pointer)),
                    y0 + (int16_t)((barSize + barWidth)*sin(_Pointer)), GFXFF);
 
}

void Gauges::MarkArrowIn(float x0, float y0, float barSize, float barWidth, float pointer, char tag, float theta, uint16_t color) {

  /* 
    Inside arrow (pointing out)
                 
       p4        
       /\     
      /  \       
     /    \
    p2-p1-p3 
        
  */
        
  float _Pointer = (pointer + theta);
  //float _Factor = barSize / (barSize - 1.5f * barWidth);
  float cosA = cos (_Pointer);
  float sinA = sin (_Pointer);
  float bw1 = barSize - 1.50f * barWidth;
  float bw2 = barSize - 0.50f * barWidth;
  float bw3 = 0.33f * barWidth;
  
  float x1 = x0 + bw1 * cosA;
  float y1 = y0 + bw1 * sinA;
  float x4 = x0 + bw2 * cosA;
  float y4 = y0 + bw2 * sinA;  
  float x2 = x1 + bw3 * sinA;  
  float y2 = y1 + bw3 * -cosA;
  float x3 = x1 + bw3 * -sinA;
  float y3 = y1 + bw3 * cosA;
  
  gdraw.fillTriangle (x4, y4, x3, y3, x2, y2, color);
  gdraw.drawTriangle (x4, y4, x3, y3, x2, y2, TFT_BLACK);

  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 + (int16_t)((barSize - 2.0f * barWidth) * cos (_Pointer)) - 1,
                    y0 + (int16_t)((barSize - 2.0f * barWidth)*sin(_Pointer)) - 1, GFXFF);
  gdraw.drawString ((String)tag, x0 + (int16_t)((barSize - 2.0f * barWidth) * cos (_Pointer)) + 1,
                    y0 + (int16_t)((barSize - 2.0f * barWidth)*sin(_Pointer)) + 1, GFXFF);
  gdraw.setTextColor (color);
  gdraw.drawString ((String)tag, x0 + (int16_t)((barSize - 2.0f * barWidth) * cos (_Pointer)),
                    y0 + (int16_t)((barSize - 2.0f * barWidth)*sin(_Pointer)), GFXFF);
}

void Gauges::MarkRbar(float x0, float y0, float barSize, float barWidth, float pointer, char tag, float theta, uint16_t color) {

  /* 
    Radial Bar

   p4-p5-p6
   |     |    
   |     |       
   |     |        
   |     | 
   p1-p2-p3

  */
  float _Pointer = (pointer + theta);
  
  float cosA = cos(_Pointer);
  float sinA = sin(_Pointer);
  
  float bw1 = barSize - 1.25f * barWidth;
  float bw2 = barSize + 0.25f * barWidth;
  float bw3 = 0.250 * barWidth;
  
  float x2 = x0 + bw1 * cosA;
  float y2 = y0 + bw1 * sinA;
  float x5 = x0 + bw2 * cosA;
  float y5 = y0 + bw2 * sinA;

  fillLine (x5, y5, x2, y2, color, bw3, NONE, gdraw.alphaBlend (96, TFT_BLACK, color), 1);
  
  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_WHITE);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 + (int16_t)((barSize - 0.50f * barWidth) * cos(_Pointer)) - 1,
                    y0 + (int16_t)((barSize - 0.50f * barWidth) * sin(_Pointer)) - 1 , GFXFF);
  gdraw.drawString ((String)tag, x0 + (int16_t)((barSize - 0.50f * barWidth) * cos(_Pointer)) + 1,
                    y0 + (int16_t)((barSize - 0.50f * barWidth)*sin(_Pointer)) + 1, GFXFF);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.drawString ((String)tag, x0 + (int16_t)((barSize - 0.50f * barWidth)*cos(_Pointer)),
                    y0 + (int16_t)((barSize - 0.50f * barWidth)*sin(_Pointer)), GFXFF);
}

void Gauges::MarkRbarShort(float x0, float y0, float barSize, float barWidth, float pointer, char tag, float theta, uint16_t color) {

  /* 
    Short Radial Bar

   p4-p5-p6
   |     |    
   |     |       
   |     |        
   |     | 
   p1-p2-p3

  */
  float _Pointer = (pointer + theta);
  
  float cosA = cos(_Pointer);
  float sinA = sin(_Pointer);
    
  float bw1 = barSize - 0.125f * barWidth;
  float bw2 = barSize - 0.875f * barWidth;
  float bw3 = 0.240f * barWidth;
  
  float x2 = x0 + bw1 * cosA;
  float y2 = y0 + bw1 * sinA;
  float x5 = x0 + bw2 * cosA;
  float y5 = y0 + bw2 * sinA;
  
  fillLine (x5, y5, x2, y2, color, bw3, NONE, gdraw.alphaBlend (96, TFT_BLACK, color), 1);

  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_WHITE);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 + (int16_t)((barSize - 0.50f * barWidth) * cos(_Pointer)) - 1,
                    y0 + (int16_t)((barSize - 0.50f * barWidth) * sin(_Pointer)) - 1 , GFXFF);
  gdraw.drawString ((String)tag, x0 + (int16_t)((barSize - 0.50f * barWidth) * cos(_Pointer)) + 1,
                    y0 + (int16_t)((barSize - 0.50f * barWidth)*sin(_Pointer)) + 1, GFXFF);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.drawString ((String)tag, x0 + (int16_t)((barSize - 0.50f * barWidth)*cos(_Pointer)),
                    y0 + (int16_t)((barSize - 0.50f * barWidth)*sin(_Pointer)), GFXFF);
}

void Gauges::MarkRdot(float x0, float y0, float barSize, float barWidth, float pointer, char tag, float theta, uint16_t color) {

  float _Pointer = (pointer + theta);
  
  float x1 = x0 + (barSize - 0.50f * barWidth)* cos (_Pointer);
  float y1 = y0 + (barSize - 0.50f * barWidth )* sin (_Pointer);
  
  gdraw.fillCircle (x1, y1, 0.25f * barWidth, color);
  gdraw.drawCircle (x1, y1, 0.25f * barWidth, TFT_BLACK);

  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x1, y1, GFXFF);
}

void Gauges::MarkNeedle (float x0, float y0, float barSize, float barWidth, float pointer, char tag, float theta, uint16_t color) {

/*
  Needle (triangular)
  
  p1_____________________________
   |                             \_______________________
   |                                                     \_______________
   |                                                                     \p3
   |                                                      _______________/              
   |                               ______________________/                
  p2_____________________________/
  
  */
  
  float _Pointer = (pointer + theta);
  
  float bw1 = barSize - 0.50f * barWidth;  
  float bw2 = 0.40f * barWidth;  
  
  float _angleA = _Pointer - HALF_PI;
  float _angleB = _Pointer + HALF_PI;
  
  float x1 = x0 + bw2 * cos(_angleA);
  float y1 = y0 + bw2 * sin(_angleA);
  float x2 = x0 + bw2 * cos(_angleB);
  float y2 = y0 + bw2 * sin(_angleB);
  float x3 = x0 + bw1 * cos(_Pointer);
  float y3 = y0 + bw1 * sin(_Pointer);

  gdraw.fillTriangle (x1, y1, x2, y2, x3, y3, color);
  drawLine (x1, y1, x2, y2, TFT_BLACK, 1, NONE);
  drawLine (x2, y2, x3, y3, TFT_BLACK, 1, NONE);
  drawLine (x3, y3, x1, y1, TFT_BLACK, 1, NONE);
  
  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 - 1, y0 - 1, GFXFF);
  gdraw.drawString ((String)tag, x0 + 1, y0 - 1, GFXFF);
  gdraw.setTextColor (color);
  gdraw.drawString ((String)tag, x0, y0, GFXFF);
}

void Gauges::MarkIndex(float x0, float y0, float barSize, float barWidth, float pointer, char tag, float theta, uint16_t color) {

 /*
  Index bar (line segment)
  
  p1_____________________________p2

  */
  
  float _Pointer = (pointer + theta);

  float x1 = x0 + 0.5f * barSize * cos(_Pointer);
  float y1 = y0 + 0.5f * barSize * sin(_Pointer);
  float x2 = x0 + (barSize - 0.25 * barWidth) * cos(_Pointer);
  float y2 = y0 + (barSize - 0.25 * barWidth) * sin(_Pointer);

  drawLine (x1, y1, x2, y2, color, 6, SHARP, TFT_BLACK, 1, SHARP);
  
  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x1 - 1, y1 - 1, GFXFF);
  gdraw.drawString ((String)tag, x1 + 1, y1 - 1, GFXFF);
  gdraw.setTextColor (color);
  gdraw.drawString ((String)tag, x1, y1, GFXFF);
}

void Gauges::MarkBugOut(float x0, float y0, float barSize, float barWidth, float pointer, char tag, float theta, uint16_t color) {

  float _Pointer = (pointer + theta);
  //float _Factor = barSize / (barSize - 1.5f * barWidth);
  
  //float _barScale = barWidth * 0.5f + barSize;  
  //float _PointerX = _Pointer - 0.080f;  
  
  /*
  
    Outside radial bug
    
  	p3-p8 p6 p9-p4
	|   \    /   |
	|    \  /    | 
	|     \/     |
				  
				  
    p1----p5----p2   
       
  */

  float cosA = cos(_Pointer);
  float sinA = sin(_Pointer);

  float bw2 = barSize - 0.5f * barWidth;
										  
  float bw3 = 0.5f * barWidth;
  float bw4 = 0.25f * barWidth;
  
  float x6 = x0 + barSize * cosA;
  float y6 = y0 + barSize * sinA; 
  
							   
							   
  
  float x5 = x0 + bw2 * cosA;
  float y5 = y0 + bw2 * sinA;  
   
  float x1 = x5 + bw3 * sinA;
  float y1 = y5 + bw3 * -cosA;  
  float x2 = x5 + bw3 * -sinA;
  float y2 = y5 + bw3 * cosA; 
  float x3 = x6 + bw3 * sinA;
  float y3 = y6 + bw3 * -cosA;
  float x4 = x6 + bw3 * -sinA;
  float y4 = y6 + bw3 * cosA;  
  
  float x8 = x6 + bw4 * sinA;
  float y8 = y6 + bw4 * -cosA;
  float x9 = x6 + bw4 * -sinA;
  float y9 = y6 + bw4 * cosA;
  
  gdraw.fillTriangle (x3, y3, x8, y8, x1, y1, color);
  gdraw.fillTriangle (x1, y1, x8, y8, x5, y5, color);
  gdraw.fillTriangle (x4, y4, x9, y9, x2, y2, color);
  gdraw.fillTriangle (x2, y2, x9, y9, x5, y5, color);
                                       
  drawLine (x1, y1, x2, y2, TFT_BLACK, 1, NONE);
  drawLine (x2, y2, x4, y4, TFT_BLACK, 1, NONE);
  drawLine (x4, y4, x9, y9, TFT_BLACK, 1, NONE);
  drawLine (x9, y9, x5, y5, TFT_BLACK, 1, NONE);
  drawLine (x5, y5, x8, y8, TFT_BLACK, 1, NONE);
  drawLine (x8, y8, x3, y3, TFT_BLACK, 1, NONE);
  drawLine (x3, y3, x1, y1, TFT_BLACK, 1, NONE);

  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 + (int16_t)((barSize + barWidth) * cos(_Pointer)) - 1,
                    y0 + (int16_t)((barSize + barWidth)*sin(_Pointer)) - 1, GFXFF);
  gdraw.drawString ((String)tag, x0 + (int16_t)((barSize + barWidth) * cos(_Pointer)) + 1,
                    y0 + (int16_t)((barSize + barWidth)*sin(_Pointer)) + 1, GFXFF);
  gdraw.setTextColor (color);
  gdraw.drawString ((String)tag, x0 + (int16_t)((barSize + barWidth) * cos(_Pointer)),
                    y0 + (int16_t)((barSize + barWidth)*sin(_Pointer)), GFXFF);
}

void Gauges::MarkBugIn(float x0, float y0, float barSize, float barWidth, float pointer, char tag, float theta, uint16_t color) {

  float _Pointer = (pointer + theta);
  //float _Factor = barSize / (barSize - 1.5f * barWidth);
  
  //float _barScale = barWidth * 0.5f + barSize;  
  //float _PointerX = _Pointer - 0.080f;  
  
  /*
    Inside radial bug 
    
    p1----p5----p2
				 
				 
    |     /\    |   
	|    /  \   | 
    |   /    \  |
    p3-p8 p6 p9-p4
    
  */
 
  float cosA = cos(_Pointer);
  float sinA = sin(_Pointer);

										
  float bw1 = barSize - barWidth;
  float bw3 = 0.5f * barWidth;
  float bw4 = 0.25f * barWidth;

  float x6 = x0 + bw1 * cosA;
  float y6 = y0 + bw1 * sinA; 

							   
							   
  
  float x5 = x0 + (barSize - 0.50f * barWidth) * cosA;
  float y5 = y0 + (barSize - 0.50f * barWidth)  * sinA;  
   
  float x1 = x5 + bw3 * sinA;
  float y1 = y5 + bw3 * -cosA;  
  float x2 = x5 + bw3 * -sinA;
  float y2 = y5 + bw3 * cosA; 
  float x3 = x6 + bw3 * sinA;
  float y3 = y6 + bw3 * -cosA;
  float x4 = x6 + bw3 * -sinA;
  float y4 = y6 + bw3 * cosA;  

  float x8 = x6 + bw4 * sinA;
  float y8 = y6 + bw4 * -cosA;
  float x9 = x6 + bw4 * -sinA;
  float y9 = y6 + bw4 * cosA;

  gdraw.fillTriangle (x1, y1, x3, y3, x8, y8, color);
  gdraw.fillTriangle (x1, y1, x8, y8, x5, y5, color);
  gdraw.fillTriangle (x2, y2, x4, y4, x9, y9, color);
  gdraw.fillTriangle (x2, y2, x9, y9, x5, y5, color);
  
													 
													  
														 
  drawLine (x1, y1, x2, y2, TFT_BLACK, 1, NONE);
  drawLine (x2, y2, x4, y4, TFT_BLACK, 1, NONE);
  drawLine (x4, y4, x9, y9, TFT_BLACK, 1, NONE);
  drawLine (x9, y9, x5, y5, TFT_BLACK, 1, NONE);
  drawLine (x5, y5, x8, y8, TFT_BLACK, 1, NONE);
  drawLine (x8, y8, x3, y3, TFT_BLACK, 1, NONE);
  drawLine (x3, y3, x1, y1, TFT_BLACK, 1, NONE);

  gdraw.setFreeFont (TEXTSIZE);
  gdraw.setTextColor (TFT_BLACK);
  gdraw.setTextDatum (MC_DATUM);
  gdraw.drawString ((String)tag, x0 + (int16_t)((barSize - 2.0f * barWidth) * cos (_Pointer)) - 1,
                    y0 + (int16_t)((barSize - 2.0f * barWidth)*sin(_Pointer)) - 1, GFXFF);
  gdraw.drawString ((String)tag, x0 + (int16_t)((barSize - 2.0f * barWidth) * cos (_Pointer)) + 1,
                    y0 + (int16_t)((barSize - 2.0f * barWidth)*sin(_Pointer)) + 1, GFXFF);
  gdraw.setTextColor (color);
  gdraw.drawString ((String)tag, x0 + (int16_t)((barSize - 2.0f * barWidth) * cos (_Pointer)),
                    y0 + (int16_t)((barSize - 2.0f * barWidth)*sin(_Pointer)), GFXFF);
}

