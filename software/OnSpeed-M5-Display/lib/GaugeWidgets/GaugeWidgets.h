/*
Copyright 2021 V.R. Little

Permission is hereby granted, free of charge, to any person provided a copy of this software and associated documentation files
(the "Software") to use, copy, modify, or merge copies of the Software for non-commercial purposes, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS 
BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF 
OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

/*
   GaugeWidgets_M5.h - Library for drawing engine gauges.
    by V.R. ("Voltar") Little
    Version 2.0, April 2, 2020
    Version 2.1, July 13, 2020: 
      * Updated gradMarks function to support negative gauge values.  
      * Changed gradMarks colors from white to light grey.
      * Made BAR_LONG thinner for more precision.  
    Version 2.2, October 11, 2020:
      * Updated gradMarks function to support user defined graduation marks:
          Any gradMarks > 1, then both major and minor graduation marks are automatically generated(backwards compatibility mode).
          If gradMarks == 1, then the setGradMarks methods are used to specify major and minor graduation mark lengths and colors.
    Version 3.0, December 23, 2020:     
      * Forked from huVVer.tech M5stack Library.
      * Added setLine methods (color, width and end).
      * Added setEdge methods (color, width and end).
      * Added drawLine methods.
      * Added drawTriangle methods.
      * Added drawQuadrangle methods.
      * Added drawRectangle methods.
      * Added gradMarkRectange methods.
      * Added drawArc methods.
      * Added gradMarcArc methods.
      * Changed syntax of setGradMarks methods to support more flexible methods.
      * General cleanup and minor bug fixes.
      * Programs using gradMarks may be required to use new method syntax.
      
    Version 3.1, March 1, 2021:
      * Now a single library handles either M5Stack or huVVer AVI devices, as define in the "DeviceDefines.h" file.  
      
    Version 3.2, December 24, 2021:
      * Added INDEX pointer type to arc/circle gauges. VRL
      
*/

#ifndef _GAUGEWIDGETS_H_
  #define _GAUGEWIDGETS_H_
  
  #include "DeviceDefines.h"
  
    #ifdef DEVICE_M5
     #include <M5Stack.h>
    #else 
     #include "VVLink.h"
    #endif
    
    /*
     Helpful definitions for various gauge markers
     */
    #define ARROW_LEFT 1
    #define ARROW_RIGHT 2

    #define ARROW_TOP 1
    #define ARROW_BOTTOM 2

    #define ARROW_OUT 1
    #define ARROW_IN 2

    #define BAR_LONG 3
    #define BAR_SHORT 4

    #define BUG_LEFT 5
    #define BUG_RIGHT 6

    #define BUG_TOP 5
    #define BUG_BOTTOM 6

    #define BUG_OUT 5
    #define BUG_IN 6

    #define ROUND_DOT 7

    #define NEEDLE 8
    #define INDEX 9
    
    #define NOFILL 1 // Almost black color

    //Line end definitions
    #define NONE 0	// blunt ends (squared)
    #define SHARP 1	// pointed ends (trianglular)
    #define ROUND 2	// rounded ends (circular)

    /*
     Define the number of allowed pointers & colored range bars.
     Beware of the memory requirements when upscaling these values!
     */
    #define NUM_POINTERS 8
    #define NUM_RANGES 5

    class Gauges {

      public:
        Gauges();
        
        //
        // DRAWING PRIMITIVES
        //
        
        //
        // drawLine morphs
        //
        void drawLine (int16_t x0, int16_t y0, int16_t x1, int16_t y1,                          // line with full parameters
                       uint16_t lineColor, 
                       uint16_t lineWidth = 1, uint8_t lineEnd = NONE,
                       uint16_t edgeColor = NONE, 
                       uint16_t edgeWidth = 0, uint8_t edgeEnd = NONE);                     
                       
        void fillLine (int16_t x0, int16_t y0, int16_t x1, int16_t y1,                          // line with full parameters
                       uint16_t lineColor, 
                       uint16_t lineWidth = 1, uint8_t lineEnd = NONE,
                       uint16_t edgeColor = TFT_WHITE, 
                       uint16_t edgeWidth = 1, uint8_t edgeEnd = NONE);  
                       
        void drawEdge (int16_t x0, int16_t y0, int16_t x1, int16_t y1,                          // edgeline with full parameters
                       uint16_t edgeColor, 
                       uint16_t edgeWidth = 1, uint8_t edgeEnd = NONE);
        //
        // Triangle morphs
        //
        void drawTriangle (int16_t px1, int16_t py1, int16_t px2, int16_t py2,                  // filled triangle with predefined parameters
                           int16_t px3, int16_t py3, 
                           uint16_t edgeColor = TFT_WHITE, 
                           uint16_t edgeWidth = 1,  uint8_t edgeEnd = NONE);	
                    
                    
        void fillTriangle (int16_t px1, int16_t py1, int16_t px2, int16_t py2,                  // filled triangle with predefined parameters
                           int16_t px3, int16_t py3, 
                           uint16_t fillColor,
                           uint16_t edgeColor = TFT_WHITE, 
                           uint16_t edgeWidth = 1,  uint8_t edgeEnd = NONE);	                       

        //
        // Quadrangle morphs                                         
        //                    
        void drawQuadrangle (int16_t px1, int16_t py1, int16_t px2, int16_t py2,  	            // Draw quadrangle with all parameters	
                             int16_t px3, int16_t py3, int16_t px4, int16_t py4,                	
                             uint16_t edgeColor,
                             uint16_t edgeWidth = 1, uint8_t edgeEnd = NONE);		

        void fillQuadrangle (int16_t px1, int16_t py1, int16_t px2, int16_t py2, 	            // Draw quadrangle with predefined 	
                             int16_t px3, int16_t py3, int16_t px4, int16_t py4,                // line width and end	
                             uint16_t fillColor,
                             uint16_t edgeColor = TFT_WHITE,
                             uint16_t edgeWidth = 1, uint8_t edgeEnd = NONE);	
                             
        //
        // Rectangle morphs	
        //  
        void drawRectangle (int16_t x0, int16_t y0, int16_t width, int16_t height,            // 	
                            uint16_t edgeColor = TFT_WHITE, 
                            uint16_t edgeWidth = 1, uint8_t edgeEnd = NONE);
                            
        void fillRectangle (int16_t x0, int16_t y0, int16_t width, int16_t height,            // 	
                            uint16_t fillColor,
                            uint16_t edgeColor = TFT_WHITE, 
                            uint16_t edgeWidth = 1, uint8_t edgeEnd = NONE);
                            
        void gradMarkRectangle (int16_t x0, int16_t y0, int16_t width, int16_t height);
        
        void gradMarkRectangle (int16_t x0, int16_t y0, int16_t width, int16_t height,
                            int16_t gradMarks, uint16_t gradMajorColor, uint16_t gradMajorLength, uint16_t gradMajorWidth,  
                            uint16_t gradMinorColor, uint16_t gradMinorLength, uint16_t gradMinorWidth, uint8_t gradLineEnd = NONE);

        //
        // Arc and circle morphs
        //
        void drawArc (int16_t x0, int16_t y0, int16_t radius,                                  // arc using predefined parameters
                      float startAngle, float arcAngle, 
                      uint16_t lineColor, uint16_t lineWidth = 1,    
                      uint16_t edgeColor = NONE, uint16_t edgeWidth = 0, uint8_t edgeEnd = NONE);
                      
        void fillArc (int16_t x0, int16_t y0, int16_t radius,                                  // arc using predefined parameters
                      float startAngle, float arcAngle, 
                      uint16_t lineColor, uint16_t lineWidth = 1,    
                      uint16_t edgeColor = TFT_WHITE, uint16_t edgeWidth = 1, uint8_t edgeEnd = NONE);  
                
        void gradMarkArc (int16_t x0, int16_t y0, int16_t radius, float starAngle, float arcAngle); 
                            
        void gradMarkArc (int16_t x0, int16_t y0, int16_t radius, float starAngle, float arcAngle,
                          int16_t gradMarks, uint16_t gradMajorColor, uint16_t gradMajorLength, uint16_t gradMajorWidth,  
                          uint16_t gradMinorColor, uint16_t gradMinorLength, uint16_t gradMinorWidth, uint8_t gradLineEnd = NONE);

        //
        // Gauge methods
        //
        void clearPointers ();                                                                  // clear all pointers
        void clearRanges ();                                                                    // clear all ranges
        void setPointer (uint8_t Num, int16_t Value, uint8_t Type, uint16_t color, char Tag);   // set a pointer
        void setRange (uint8_t Num, bool Valid, int16_t Top, int16_t Bot, uint16_t color);      // set color ranges
        void vBarGraph (int16_t x0, int16_t y0, int16_t BarSize, int16_t barWidth,              // draw a vertical bar graph   
                        int16_t maxDisplay, int16_t minDisplay, int16_t gradMarks = 0);      
        void hBarGraph (int16_t x0, int16_t y0, int16_t barSize, int16_t barWidth,              // draw a horizontal bar graph
                        int16_t maxDisplay, int16_t minDisplay, int16_t gradMarks = 0);
        void arcGraph (int16_t x0, int16_t y0, int16_t barSize, int16_t barWidth,               // draw an arc graph (cw or ccw)
                       int16_t maxDisplay, int16_t minDisplay, int16_t startAngle, 
                       int16_t arcAngle, bool clockWise, int16_t gradMarks = 0);
        int16_t printNum (String value, int16_t x0, int16_t y0, int16_t width,                  // scalable, rotable vector font
                          int16_t height, int16_t roll, uint16_t color, uint8_t datum,          // returns total string length in pixels
                          uint16_t lineWidth = 1, uint8_t lineEnd = SHARP);                     // for numbers and symbols only.
        void setGradMarks (uint16_t MjColor, uint16_t MjLen, uint16_t MjWidth,                  // sets parameters for major and minor
                           uint16_t MnColor, uint16_t MnLen, uint16_t MnWidth,                  // graduation marks and sets line end
                           uint8_t gLineEnd = NONE);                                            // NONE, SHARP, ROUND
        void setGradMarks (int16_t gMarks);
        void clearGradMarks();                                                                  // sets all grad mark parameters to 0.
        
        //
        // Public variables.
        //
         
        //
        // Used for gauge drawing only. They may be directly addresses from the main program.
        //

        bool     clockWise;  
        bool     rangeValid[NUM_RANGES + 1];
        int32_t  rangeTop [NUM_RANGES + 1];
        int32_t  rangeBot [NUM_RANGES + 1];
        int32_t  rangeColor [NUM_RANGES + 1];
        int32_t  pointerValue [NUM_POINTERS + 1];
        int32_t  pointerType [NUM_POINTERS + 1];
        int32_t  pointerColor [NUM_POINTERS + 1];
        char     pointerTag [NUM_POINTERS + 1];
            
        int16_t  maxDisplay, minDisplay; 
        int16_t  barWidth; 
        int16_t  barSize;

        // The following variables are accessible after drawing a gauge, and are used to help position additional text.
        int16_t topDatumX, topDatumY, btmDatumX, btmDatumY; // endpoint text datum helpers
        
        //
        //used for Gauges, Lines, Triangles, Rectangles and Arcs.
        //    
        int16_t  gradMarks;         // number of intervals between marks
        uint16_t gradMajorColor ;  
        uint16_t gradMajorLength;
        uint16_t gradMajorWidth;
        uint16_t gradMinorColor ;
        uint16_t gradMinorLength; 
        uint16_t gradMinorWidth;
        uint8_t  gradLineEnd;		// graduation marks have their own line end type
        
        uint16_t fillColor, lineColor, edgeColor;
        uint16_t lineWidth, edgeWidth;     
        uint8_t  lineEnd, edgeEnd;

      private:

        /*
         Pointer types for vertical or horizontal bar graph
         */
        void MarkArrowLeft (int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color);  // PointerType 1 for Vertical graphs
        void MarkArrowRight (int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color); // PointerType 2 for Vertical graphs
        void MarkHbar (int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color);       // PointerType 3 for Vertical graphs
        void MarkHbarShort (int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color);  // PointerType 4 for Vertical graphs
        void MarkBugLeft (int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color);    // PointerType 5 for Vertical graphs
        void MarkBugRight (int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color);   // PointerType 6 for Vertical graphs
        void MarkHdot (int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color);       // PointerType 7 for Vertical graphs

        void MarkArrowTop (int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color);   // PointerType 1 for Horizontal graphs
        void MarkArrowBottom (int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color);// PointerType 2 for Horizontal graphs
        void MarkVbar (int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color);       // PointerType 3 for Horizontal graphs
        void MarkVbarShort (int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color);  // PointerType 4 for Horizontal graphs
        void MarkBugTop (int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color);     // PointerType 5 for Horizontal graphs
        void MarkBugBot (int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color);     // PointerType 6 for Horizontal graphs
        void MarkVdot (int16_t x0, int16_t y0, int16_t barWidth, int16_t pointer, char tag, uint16_t color);       // PointerType 7 for Horizontal graphs
       
        /*
          Pointer types for radial arc or circle graphs
         */   
        void MarkArrowOut (float x0, float y0, float barSize, float barWidth, float pointer,                       // PointerType 1 for arc graphs
                           char tag, float theta, uint16_t color);                                                 
        void MarkArrowIn (float x0, float y0, float barSize, float barWidth, float pointer,                        // PointerType 2 for arc graphs
                          char tag, float theta, uint16_t color);                                                  
        void MarkRbar (float x0, float y0, float barSize, float barWidth, float pointer,                           // PointerType 3 for arc graphs
                       char tag, float theta, uint16_t color);                                                     
        void MarkRbarShort (float x0, float y0, float barSize, float barWidth, float pointer,                      // PointerType 4 for arc graphs
                            char tag, float theta, uint16_t color);                                                
        void MarkBugOut (float x0, float y0, float barSize, float barWidth, float pointer,                         // PointerType 5 for arc graphs 
                           char tag, float theta, uint16_t color);                                                        
        void MarkBugIn (float x0, float y0, float barSize, float barWidth, float pointer,                          // PointerType 6 for arc graphs 
                           char tag, float theta, uint16_t color);                                                     
        void MarkRdot (float x0, float y0, float barSize, float barWidth, float pointer,                           // PointerType 7 for arc graphs 
                            char tag, float theta, uint16_t color);                                                                             
        void MarkNeedle (float x0, float y0, float barSize, float barWidth, float pointer,                         // PointerType 8 for arc graphs 
                         char tag, float theta, uint16_t color);                                                        
        void MarkIndex (float x0, float y0, float barSize, float barWidth, float pointer,                          // PointerType 9 for arc graphs 
                         char tag, float theta, uint16_t color);                                                        
	};
  #endif  // _GAUGEWIDGETS_M5_H_ //