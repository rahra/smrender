#ifndef COLORS_C
#define COLORS_C


struct color_def
{
   int col;
   char *name;
};


static struct color_def color_def_[] =
{
   // X11 color space,
   // see http://www.xfree86.org/current/X.7.html#toc10 
   // see /usr/share/X11/rgb.txt
   // see http://en.wikipedia.org/wiki/X11_color_names
   //
   // Suggested color use in comments is according to
   // http://wiki.openstreetmap.org/wiki/User:Skippern/INT-1
   //

   {0x00000000, "black"},              // Border: Gray, Buildings, General Symbols, Text: Black 
   {0x00000080, "navy"},
   {0x0000008B, "darkblue"},
   {0x000000CD, "mediumblue"},
   {0x000000FF, "blue"},
   {0x00006400, "darkgreen"},
   {0x00008000, "green"},
   {0x00008080, "teal"},
   {0x00008B8B, "darkcyan"},           // Special 
   {0x0000BFFF, "deepskyblue"},
   {0x0000CED1, "darkturquoise"},
   {0x0000FA9A, "mediumspringgreen"},
   {0x0000FF00, "lime"},               // Light: Green 
   {0x0000FF7F, "springgreen"},
   {0x0000FFFF, "aqua"},
   {0x0000FFFF, "cyan"},
   {0x00191970, "midnightblue"},
   {0x001E90FF, "dodgerblue"},
   {0x0020B2AA, "lightseagreen"},
   {0x00228B22, "forestgreen"},        // Border: Green, Text: Green 
   {0x002E8B57, "seagreen"},
   {0x002F4F4F, "darkslategray"},
   {0x0032CD32, "limegreen"},
   {0x003CB371, "mediumseagreen"},
   {0x0040E0D0, "turquoise"},
   {0x004169E1, "royalblue"},
   {0x004682B4, "steelblue"},
   {0x00483D8B, "darkslateblue"},
   {0x0048D1CC, "mediumturquoise"},
   {0x004B0082, "indigo"},
   {0x00556B2F, "darkolivegreen"},
   {0x005F9EA0, "cadetblue"},
   {0x006495ED, "cornflowerblue"},
   {0x0066CDAA, "mediumaquamarine"},
   {0x00696969, "dimgray"},
   {0x006A5ACD, "slateblue"},
   {0x006B8E23, "olivedrab"},
   {0x00708090, "slategray"},
   {0x00778899, "lightslategray"},
   {0x007B68EE, "mediumslateblue"},
   {0x007CFC00, "lawngreen"},
   {0x007FFF00, "chartreuse"},
   {0x007FFFD4, "aquamarine"},
   {0x00800000, "maroon"},
   {0x00800080, "purple"},
   {0x00808000, "olive"},
   {0x00808080, "gray"},
   {0x0087CEEB, "skyblue"},            // Shallow Water 
   {0x0087CEFA, "lightskyblue"},
   {0x008A2BE2, "blueviolet"},
   {0x008B0000, "darkred"},
   {0x008B008B, "darkmagenta"},        // Amenities, Border: Magenta, Light: Magenta, Services, Text: Magenta 
   {0x008B4513, "saddlebrown"},
   {0x008FBC8F, "darkseagreen"},       // Border: Green (shading), Tidal Zone 
   {0x0090EE90, "lightgreen"},
   {0x009370DB, "mediumpurple"},
   {0x009400D3, "darkviolet"},
   {0x0098FB98, "palegreen"},
   {0x009932CC, "darkorchid"},
   {0x009ACD32, "yellowgreen"},
   {0x00A0522D, "sienna"},
   {0x00A52A2A, "brown"},
   {0x00A9A9A9, "darkgray"},
   {0x00ADD8E6, "lightblue"},          // Deep Water 
   {0x00ADFF2F, "greenyellow"},
   {0x00AFEEEE, "paleturquoise"},
   {0x00B0C4DE, "lightsteelblue"},
   {0x00B0E0E6, "powderblue"},
   {0x00B22222, "firebrick"},
   {0x00B8860B, "darkgoldenrod"},      // Buildings, Urban Areas 
   {0x00BA55D3, "mediumorchid"},
   {0x00BC8F8F, "rosybrown"},
   {0x00BDB76B, "darkkhaki"},
   {0x00C0C0C0, "silver"},
   {0x00C71585, "mediumvioletred"},
   {0x00CD5C5C, "indianred"},
   {0x00CD853F, "peru"},
   {0x00D2691E, "chocolate"},
   {0x00D2B48C, "tan"},
   {0x00D3D3D3, "lightgray"},          // Border: Gray (shading) 
   {0x00D8BFD8, "thistle"},            // Border: Magenta (shading) 
   {0x00DA70D6, "orchid"},
   {0x00DAA520, "goldenrod"},
   {0x00DB7093, "palevioletred"},
   {0x00DC143C, "crimson"},
   {0x00DCDCDC, "gainsboro"},
   {0x00DDA0DD, "plum"},
   {0x00DEB887, "burlywood"},
   {0x00E0FFFF, "lightcyan"},
   {0x00E6E6FA, "lavender"},
   {0x00E9967A, "darksalmon"},
   {0x00EE82EE, "violet"},
   {0x00EEE8AA, "palegoldenrod"},
   {0x00F08080, "lightcoral"},
   {0x00F0E68C, "khaki"},
   {0x00F0F8FF, "aliceblue"},          // Medium Water 
   {0x00F0FFF0, "honeydew"},
   {0x00F0FFFF, "azure"},
   {0x00F4A460, "sandybrown"},
   {0x00F5DEB3, "wheat"},
   {0x00F5F5DC, "beige"},
   {0x00F5F5F5, "whitesmoke"},
   {0x00F5FFFA, "mintcream"},
   {0x00F8F8FF, "ghostwhite"},
   {0x00FA8072, "salmon"},
   {0x00FAEBD7, "antiquewhite"},
   {0x00FAF0E6, "linen"},
   {0x00FAFAD2, "lightgoldenrodyellow"},
   {0x00FDF5E6, "oldlace"},
   {0x00FF0000, "red"},                // Light: Red, Text: Red 
   {0x00FF00FF, "fuchsia"},
   {0x00FF00FF, "magenta"},
   {0x00FF1493, "deeppink"},
   {0x00FF4500, "orangered"},
   {0x00FF6347, "tomato"},
   {0x00FF69B4, "hotpink"},
   {0x00FF7F50, "coral"},
   {0x00FF8C00, "darkorange"},
   {0x00FFA07A, "lightsalmon"},
   {0x00FFA500, "orange"},
   {0x00FFB6C1, "lightpink"},
   {0x00FFC0CB, "pink"},
   {0x00FFD700, "gold"},
   {0x00FFDAB9, "peachpuff"},
   {0x00FFDEAD, "navajowhite"},        // Land 
   {0x00FFE4B5, "moccasin"},
   {0x00FFE4C4, "bisque"},
   {0x00FFE4E1, "mistyrose"},
   {0x00FFEBCD, "blanchedalmond"},
   {0x00FFEFD5, "papayawhip"},
   {0x00FFF0F5, "lavenderblush"},
   {0x00FFF5EE, "seashell"},
   {0x00FFF8DC, "cornsilk"},
   {0x00FFFACD, "lemonchiffon"},
   {0x00FFFAF0, "floralwhite"},
   {0x00FFFAFA, "snow"},
   {0x00FFFF00, "yellow"},             // Artificial Islands, Light: Yellow 
   {0x00FFFFE0, "lightyellow"},
   {0x00FFFFF0, "ivory"},
   {0x00FFFFFF, "white"},              // Deeper Water 

   {0x7f000000, "transparent"},
   {0xffffff, "bgcolor"},

   {-1, (void*)0}
};


#define MAXCOLOR ((int) (sizeof(color_def_) / sizeof(struct color_def) - 1))

#endif

