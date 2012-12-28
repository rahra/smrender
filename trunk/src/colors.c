#ifndef COLORS_C
#define COLORS_C


struct color_def
{
   int col;
   char *name;
};


static struct color_def color_def_[] =
{
   {0x000000, "black"},   // Border: Gray, Buildings, General Symbols, Text: Black 
   {0x008b8b, "darkcyan"},   // Special 
   {0x00ff00, "lime"},   // Light: Green 
   {0x228b22, "forestgreen"},   // Border: Green, Text: Green 
   {0x87ceeb, "skyblue"},   // Shallow Water 
   {0x8b008b, "darkmagenta"},   // Amenities, Border: Magenta, Light: Magenta, Services, Text: Magenta 
   {0x8fbc8f, "darkseagreen"},   // Tidal Zone 
   {0xadd8e6, "lightblue"},   // Deep Water 
   {0xb8860b, "darkgoldenrod"},   // Buildings, Urban Areas 
   {0xc1e8cd, "darkseagreen"},   // Border: Green (shading) 
   {0xd3d3d3, "lightgray"},   // Border: Gray (shading) 
   {0xd8bfd8, "thistle"},   // Border: Magenta (shading) 
   {0xf0f8ff, "aliceblue"},   // Medium Water 
   {0xff0000, "red"},   // Light: Red, Text: Red 
   {0xffdead, "navajowhite"},   // Land 
   {0xffff00, "yellow"},   // Artificial Islands, Light: Yellow 
   {0xfffffe, "white"},   // Deeper Water 

   {0x7f000000, "transparent"},
   {0xffffff, "bgcolor"},

   {-1, (void*)0}
};


#define MAXCOLOR (sizeof(color_def_) / sizeof(struct color_def) - 1)

#endif

