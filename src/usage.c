/* Copyright 2024-2025 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
 *
 * This file is part of smrender.
 *
 * Smrender is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * Smrender is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with smrender. If not, see <http://www.gnu.org/licenses/>.
 */

/*! \file usage.c
 * This file contains the usage output
 *
 *  \author Bernhard R. Fischer, <bf@abenteuerland.at>
 *  \date 2025/01/11
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#ifdef HAVE_CAIRO
#include <cairo.h>
#endif


static const char *usage_txt_ =
   "\n"
   "Rendering Window:\n"
   "   <window>      := <center> | <bbox>\n"
   "   <bbox>        := <left lower>:<right upper>\n"
   "   <left lower>  := <coords>\n"
   "   <right upper> := <coords>\n"
   "   <center>      := <coords>:<size>\n"
   "   <coords>      := <lat>:<lon>\n"
   "   <size>        := <scale> | <length>'d' | <length>'m'\n"
   "   <scale>       := scale of chart\n"
   "   <length>      := length of mean latitude in either degrees ('d') or nautical miles ('m')\n"
   "\n"
   "Inuput/Output Options\n"
   "   --in <osm_inpur>\n"
   "   -i <osm_input> ......... OSM input data (default is stdin).\n"
   "\n"
   "   --filter\n"
   "   -f ..................... Use loading filter.\n"
   "\n"
   "   --kap <filename>\n"
   "   -k <filename> .......... Generate KAP file.\n"
   "\n"
   "   --kap-header <filename>\n"
   "   -K <filename> .......... Generate KAP header file.\n"
   "\n"
   "   -M ..................... Input file is memory mapped (default).\n"
   "   -m ..................... Input file is read into heap memory.\n"
   "\n"
   "   --out <image_file>\n"
   "   -o <image_file> ........ Name of output file. The extensions determines the output format.\n"
   "                            Currently supported formats: .PDF, .PNG, .SVG.\n"
   "   -O <pdf_file> .......... Filename of output PDF file (DEPRECATED: use -o).\n"
   "\n"
   "   --write <osm_file>\n"
   "   -w <osm_file> .......... Output internal OSM database to file at the end of processing.\n"
   "\n"
   "   --index\n"
   "   -x ..................... Use index file to speed up data loading.\n"
   "\n"
   "Rules Options:\n"
   "   --rules <rules_file>\n"
   "   -r <rules_file> ........ Rules file ('rules.osm' is default).\n"
   "                            Set <rules_file> to 'none' to run without rules.\n"
   "\n"
   "   --out-rules <file>\n"
   "   -R <file> .............. Output all rules to <file> in OSM or JSON format dependent on its extension.\n"
   "   -S <file> .............. Output processed rules in rendering order to <file> in JSON format (DEPRECATED: use -R).\n"
   "\n"
   "   --id-offset <offset>\n"
   "   -N <offset> ............ Add numerical <offset> to all IDs in output data.\n"
   "\n"
   "   --id-positive\n"
   "   -n ..................... Output IDs as positive values only.\n"
   "\n"
   "Rendering Control:\n"
   "   --all-nodes\n"
   "   -a ..................... Render all nodes, otherwise only nodes which are\n"
   "                            on the page are rendered.\n"
   "\n"
   "   --border <border>\n"
   "   -B <border> ............ Add an additional border to the page.\n"
   "\n"
   "   --bgcolor <color>\n"
   "   -b <color> ............. Choose background color ('white' is default).\n"
   "\n"
   "   --dpi <density>\n"
   "   -d <density> ........... Set image density (300 is default).\n"
   "\n"
   "   --grid <grd>[:<t>[:<s>]]\n"
   "   -g <grd>[:<t>[:<s>]]     Distance of grid/ticks/subticks in minutes.\n"
   "   --no-grid\n"
   "   -G ..................... Do not generate grid nodes/ways.\n"
   "\n"
   "   --landscape\n"
   "   -l ..................... Select landscape output. Only useful with option -P.\n"
   "\n"
   "   --projection <projection>\n"
   "   -p <projection> ........ Chart projection, either 'mercator' (default) or 'adams2'.\n"
   "\n"
   "   --page <format>\n"
   "   -P <format> ............ Select output page format.\n"
   "\n"
   "   --img-scale <img_scale>\n"
   "   -s <img_scale> ......... Set global image scale (default = 1.0).\n"
   "\n"
   "   --title <title>\n"
   "   -t <title> ............. Set descriptional chart title. This title will also be used\n"
   "                            as title in the PDF metadata.\n"
   "\n"
   "Logging:\n"
   "   --logfile [+]<logfile>[:<lopt]\n"
   "   -L [+]<logfile>[:<lopt]\n"
   "      ..................... Save log output to <logfile>. Option '+' means append to logfile\n"
   "                            Option <lopt> allows to be 'nologtime' to suppress timestamp.\n"
   "\n"
   "   --inc-loglevel\n"
   "   -D ..................... Increase verbosity (can be specified multiple times).\n"
   "\n"
   "   --no-color\n"
   "   -C ..................... Disable colored log output.\n"
   "   --color ................ Enable colored log output (default).\n"
   "\n"
   "   --traverse-alarm <sec> . Output progress every <sec> seconds. This may be of interest for\n"
   "                            very long running rules in huge dataets.\n"
   "\n"
   "Tile Creation (experimental):\n"
   "   --tiles <tile_info>\n"
   "   -T <tile_info> ......... Create tiles.\n"
   "      <tile_info> := <zoom_lo> [ '-' <zoom_hi> ] ':' <tile_path> [ ':' <file_type> ]\n"
   "      <file_type> := 'png' | 'jpg'\n"
   "\n"
   "Miscellaneous Options:\n"
   "   --urls\n"
   "   -u ..................... Output URLs suitable for OSM data download and exit.\n"
   "\n"
   "   --params\n"
   "   -V ..................... Show chart parameters and exit.\n"
   "\n"
   "   --version\n"
   "   -v ..................... Print version and exit.\n"
   ;


void print_version(void)
{
   printf("smrender " PACKAGE_VERSION ", (c) 2011-2025, Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>.\n"
          "See https://github.com/rahra/smrender for more information.\n");
#ifdef HAVE_CAIRO
   printf("Using libcairo %s.\n", cairo_version_string());
#else
   printf("Compiled without libcairo support.\n");
#endif
}
 

void usage(const char *s)
{
   print_version();
   printf("usage: %s [OPTIONS] <window>\n", s);
   puts(usage_txt_);
}

