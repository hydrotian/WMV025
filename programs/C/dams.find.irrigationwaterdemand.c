/*
 * Purpose: Find irrigation water demand for all dams within a basin. 
            The basin is defined by its direction file. 
  * Usage  : "\nUsage:\n%s\t"
	    "\t[year - dams] "
	    "\t[input direction file] "
	    "\t[input fraction file] "
	    "\t[input damfile (*reservoirs.firstline)] "
	    "\t[input soilfile (soilfile for current basin)] "
	    "\t[indir fluxfiles, noirrg and freeirrg] "
	    "\t[output file2 (reservoirs.upstream) gives information on which dams are upstream each cell in basin (for checking and plotting). 
	                          includes info on capacity and mean annual inflow to the reservoir]\n\n";
	    "\t[output file3 (reservoirs.extractwater) gives information on which dams water can be extracted from (for metdata.modify.runoff.c). 
                                  includes info on capacity and mean annual inflow to the reservoir]\n\n";
	    "\t[output directory for resulting files (damname.calc.irrdemand.monthly) of calculated irrigation water demand (monthly, NYears*12 numbers, unit m3s-1) 
                      for each dam in basin (for routing program)]";
	    "\t[input irrigation fraction file]";
	    "\t[file with information on VIC output file content]";
	    "\t[startyear of simulations]";
	    "\t[endyear]";
	    "\t[column number, precipitation (in flux files)]";
	    "\t[column number, et (in flux files)]\n\n";
 *          The file <input damfile> is made using dams.gages.withinbasin.make.routinput.c,
            and has the following columns:
               col row lon lat damid damname year capacity resarea ? ? ? ? height purpose 
            NB! On glitra the integer value of "H" is 72 and "I" is 73,C =67
	    BASIN[][]: rows/cols: 1-nrows/ncols
	    DAM[]: damnumber 1-ndams
 * Notes  : If in doubt, read the disclaimer.
 * Disclaimer: Feel free to use or adapt any part of this program for your own
             convenience.  However, this only applies with the understanding
             that YOU ARE RESPONSIBLE TO ENSURE THAT THE PROGRAM DOES WHAT YOU
             THINK IT SHOULD DO.  The author of this program does not in any
             way, shape or form accept responsibility for problems caused by
             the use of this code.  At any time, please feel free to discard
             this code and write your own, it's what I would do.
 * Author : Ingjerd Haddeland
 * E-mail : iha@nve.no
 * Created: December 2004, revised April 2010 (watermip2009A) and Dec 2010.
 * NBNBNBNB! this version has undergone a limited quality check, but there is 
             no 100% guarantee that it works perfectly in all basins. seems to
             be ok in the colorado and chanjiang basins, though.  
 *
e.g:
gcc -lm -Wall  /hdata/watch/iha/watch/programs/C/dams.find.irrigationwaterdemand.c
./a.out 2000 ./rout/input/35237.dir ./rout/input/35237.frac ./rout/input/35237.reservoirs.firstline ./input/soil/soil.current ../run/output/wfd/baseline ./rout/input/35237.reservoirs.upstream ./rout/input/35237.reservoirs.extractwater ../../data/dams/unh/2000 ../../data/irrigation/irr.2000.orig.gmt ../misc/fluxes.mic.2009A 1980 1999 3 6

 */

/******************************************************************************/
/*			    PREPROCESSOR DIRECTIVES                           */
/******************************************************************************/
/* changed by Tian */
#define _USE_MATH_DEFINES
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EPS 0.125		/* precision */  // res Tian 2013
#define EMPTY_STR ""		/* empty string */
#define MISSING -999		/* missing value indicator */
#define ENOERROR 0		/* no error */
#define EUSAGE 1001		/* usage error */
#define ENODOUBLE 1002		/* not a double */
#define ENOFLOAT 1003		/* not a float */
#define ENOINT 1004		/* not an integer */
#define ENOLONG 1005		/* not a long */
#define ENOSHORT 1006		/* not a short */
#define ENOCASE 1007		/* not a valid case in a switch statement */
#define EBASIN 1008	        /* invalid basin */
#define ESCAN 1009		/* error in *scanf */
#define ELAT 1010		/* wrong latitude */
#define ELON 1011		/* wrong longitude */
#define DECIMAL_PLACES 4
#define TRUE 1
#define FALSE 0
#define NSKIP 0
#define MAXDISTANCE 10         /* max distance (cells) from main stem allowed for water transportation */  // changed by Tian 2013
#define MAXLENGTH 800         /* max distance (cells) from dam to outlet */
#define MAXDAMS 50
#define IRRCUTLEVEL 0.1001    /* set sligthly above 0.1 percent */
#define NODIRECTION -1        /* NODATA_value in direction file */
/******************************************************************************/
/*			TYPE DEFINITIONS, GLOBALS, ETC.                       */
/******************************************************************************/

typedef enum {double_f, int_f, float_f, long_f, short_f} FORMAT_SPECIFIER;

typedef struct { /* BASIN[][] */
  int dam[MAXDAMS]; /* Numbered from 1 and upwards, - dams built for irrigation purposes upstream 
		  current cell. NB! Dam number 1 won't be the same dam 
                  for all cells in the basin! */ 
  int extractdam[MAXDAMS]; /* Numbered from 1 and upwards, - dams built for irrigation purposes upstream 
		  current cell, but not those dams where current cell is directly on the dam's downstream river stem.
	          Need to figure out this for the routing program to be able to extract water from the 
		  appropriate dams. */ 
  int id;
  int direction;
  int row;
  int col;
  float lat;
  float lon;
  float fraction;
  float irrfraction;
  int masl;
  int tocol;
  int torow;
  int upstream_irrdams;
  int extract_irrdams;
} CELL;

typedef struct { /* MAINSTEM[]. Numbered from 0 to mainstem_length, where 0 is the dam itself and
		    mainstem_length is the dam's outlet to the ocean */
  float lat;
  float lon;
  int row;
  int col;
} RIVER;


typedef struct { /* DAM[] */
  int id;
  int col;
  int row;
  int masl;
  float lat;
  float lon;
  int year;
  float capacity;
  float area;
  char purpose[10];
  float catcharea;
  int instcap;
  int annenergy;
  int irrareas;
  int height;
  char damname[MAXDAMS];
  float mean_annual_inflow;
  int irrdam; 
  int mainstem_length;
  RIVER *MAINSTEM;
  int mainstemdam[MAXDAMS]; /* DAM[i].mainstemdam[j]=1 means that dam j is downstream of dam i */
} LIST;


const float RES = 0.25;  // res Tian 2013
const double DEGTORAD = M_PI/180.;
const double EARTHRADIUS = 6378.; //same as in routing program

const char *usage = 
  "\nUsage:\n%s\t"
  "\t[year - dams] "
  "\t[input direction file] "
  "\t[input fraction file] "
  "\t[input damfile (*reservoirs.firstline)] "
  "\t[input soilfile (soilfile for current basin)] "
  "\t[indir fluxfiles, noirrg and freeirrg] "
  "\t[output file2 (reservoirs.upstream) gives information on which dams are upstream each cell in basin (for checking and plotting). "
  "\t[output file3 (reservoirs.extractwater) gives information on which dams water can be extracted from (for metdata.modify.runoff.c)." 
  "\t[output directory for resulting files (damname.calc.irrdemand.monthly) of calculated irrigation water demand (monthly, NYears*12 numbers, unit m3s-1) "
  "\t[input irrigation fraction file]"
  "\t[file with information on VIC output file content]"
  "\t[startyear of simulations]"
  "\t[endyear]"
  "\t[column number, precipitation (in flux files)]"
  "\t[column number, et (in flux files)]\n\n";
int status = ENOERROR;
char message[BUFSIZ+1] = "";

/******************************************************************************/
/*			      FUNCTION PROTOTYPES                             */
/******************************************************************************/
float CalcArea(float lat);
int ProcessCommandLine(int argc,char **argv,int *year,
		       char *dirfilename,char *fracfilename,
		       char *damfilename,char *soilfilename,
		       char *indir,char *outfilename2,
		       char *outfilename3,char *resdirname,char *irrfile,
		       char *flist,int *StartYear,int *EndYear,
		       int *PrecCol,int *EvapCol);
int ProcessError(void);
int FindRowsCols(char *dirfilename,int *rows,int *cols,
		 float *south,float *west);
int ReadDirFile(char *dirfilename,CELL **BASIN,
		const float RES,int *ncells);
int ReadFracFile(char *fracfilename,CELL **BASIN,
		const float RES);
int ReadDams(char *dam_file,int ndams,LIST *DAM,int *irrdams);
float FindIrr(float **,float,float,int);
int FindMainstemAndDams(LIST *DAM,CELL **BASIN,int ndams);
int CheckIfConditionsMet(LIST *DAM,CELL **BASIN,int irrdam,int ndams,int irow,
			 int icol,int *split);
int ReadFluxes(char *indir,float **CATCHMENT,int upstream_cells,
	       float *inflow,int **PARAMLIST,int Nparam,int nbytes,
	       int NDays,int NYears);
void ReadIrr(char *,float **,int);
int ReadSoil(char *soilfilename,CELL **BASIN,float **IRRYEAR,float south,
	     float north,float west,float east,int irryearcells);
int FindCells(int i,int ndams,LIST *DAM,CELL **BASIN,float **CATCHMENT,
	      const float RES,int rows,int cols,int upstream_cells);
int FindCellsSecondTime(int i,int ndams,LIST *DAM,CELL **BASIN,float **CATCHMENT,
	      const float RES,int rows,int cols,int upstream_cells);
int LineCount(const char *);
int SearchCatchment(CELL **BASIN,float **CATCHMENT,int nrows,int ncols,
		    int irow,int icol,int *upstream_cells);
int SetToMissing(int format, void *value);
int WriteOutput(char *indir,char *outfilename2,char *outfilename3,
		char *resdirname,LIST *DAM,CELL **BASIN,int rows,int cols,
		int irrdams,int year,int Nparam,int nbytes,int **PARAMLIST,
		int StartYear,int NYears,int NDays,int PrecCol,int EvapCol);
void CalculateWaterDemand(char *indir,float **IRR_DEMAND,float **GROSSIRR_DEMAND,
			  float lat,float lon,float fraction,float demandfraction,
			  int Nparam,int nbytes,int **PARAMLIST,int StartYear,
			  int NDays,int PrecCol,int EvapCol);
int  ReadList(char *,int **,int);
int  IsLeapYear(int);
/******************************************************************************/
/******************************************************************************/
/*				      MAIN                                    */
/******************************************************************************/
/******************************************************************************/
int main(int argc, char **argv)
{
  char dirfilename[BUFSIZ+1];
  char fracfilename[BUFSIZ+1];
  char outfilename2[BUFSIZ+1];
  char outfilename3[BUFSIZ+1];
  char damfilename[BUFSIZ+1];
  char soilfilename[BUFSIZ+1];
  char resdirname[BUFSIZ+1];
  char irrfile[BUFSIZ+1];
  char indir[BUFSIZ+1];
  char firryear[BUFSIZ+1];
  char flist[BUFSIZ+1];
  int rows,cols,i,j,k,ndams,irrdams,ncells;
  float **CATCHMENT;   /*Holds all cells upstream dam in question.
			 CATCHMENT[i][0]:lat,CATCHMENT[i][1]:lon,CATCHMENT[i][2]:fraction*/ 
  float **IRRYEAR;
  int **PARAMLIST;
  int year,upstream_cells,irryearcells;
  int nbytes,Nparam;
  int StartYear,EndYear,NDays,NYears;
  int PrecCol,EvapCol;
  float south,west,north,east,inflow;
  LIST *DAM = NULL;
  CELL **BASIN = NULL;

  status = ProcessCommandLine(argc,argv,&year,dirfilename,fracfilename,damfilename,
			      soilfilename,indir,outfilename2,
			      outfilename3,resdirname,irrfile,flist,
			      &StartYear,&EndYear,&PrecCol,&EvapCol);
  if (status != ENOERROR)
    goto error;

  status = FindRowsCols(dirfilename,&rows,&cols,&south,&west);
  if (status != ENOERROR)
    goto error;
  north = south + rows*RES;
  east = west + cols*RES;
 
  /* Initialize BASIN */
  BASIN = calloc((rows+2), sizeof(CELL));
  if (BASIN == NULL) {
    status = errno;
    sprintf(message, "%s: %d", __FILE__, __LINE__);
    goto error;
  }
  for (i = 0; i <= rows+2; i++) {
    BASIN[i] = calloc((cols+2), sizeof(CELL));
    if (BASIN[i] == NULL) {
      status = errno;
      sprintf(message, "%s: %d", __FILE__, __LINE__);
      goto error;
    }
  }

  for (i = 0; i <= rows+1; i++) {
    for (j = 0; j <= cols+1; j++) {
      BASIN[i][j].lat = (south + (i-0.5)*RES);
      BASIN[i][j].lon = west + (j-.5)*RES;
      BASIN[i][j].row = i;
      BASIN[i][j].col = j;
      BASIN[i][j].direction = 0;
      BASIN[i][j].fraction = 0;
      BASIN[i][j].irrfraction = 0;
      for(k=0;k<MAXDAMS;k++) {
	BASIN[i][j].dam[k] = 0;
	BASIN[i][j].extractdam[k] = 0;
      }
      BASIN[i][j].id = 0;  
      BASIN[i][j].masl = 10000.;   
      BASIN[i][j].tocol = 0;  
      BASIN[i][j].torow = 0;   
      BASIN[i][j].upstream_irrdams = 0;
      BASIN[i][j].extract_irrdams = 0;
    }
  }

  /* Read basin's direction file */
  status = ReadDirFile(dirfilename,BASIN,RES,&ncells);
  if (status != ENOERROR)
    goto error;

  /* Read basin's fraction file */
  status = ReadFracFile(fracfilename,BASIN,RES);
  if (status != ENOERROR)
    goto error;

  /* Initialize and read irr and dam info files */
  NYears=0;
  NDays=0;
  for(i=StartYear;i<=EndYear;i++) {
    NYears+=1;
    if(IsLeapYear(i)) 
      NDays+=366;
    else NDays+=365;
  }
  sprintf(firryear,"%s",irrfile);
  irryearcells = LineCount(firryear);
  Nparam = LineCount(flist);
  PARAMLIST = (int**)calloc(Nparam,sizeof(int*));
  for(i=0;i<Nparam;i++) 
    PARAMLIST[i] = (int*)calloc(4,sizeof(int));
  IRRYEAR = (float**)calloc(irryearcells,sizeof(float*));
  for(i=0;i<irryearcells;i++) 
    IRRYEAR[i] = (float*)calloc(4,sizeof(float));
  ReadIrr(firryear,IRRYEAR,irryearcells);
  ndams = LineCount(damfilename);
  DAM=calloc(ndams+1,sizeof(LIST));
  for(i=0;i<ndams+1;i++)
    DAM[i].MAINSTEM=calloc(MAXLENGTH,sizeof(RIVER)); 
  status = ReadDams(damfilename,ndams,DAM,&irrdams);
  if (status != ENOERROR)
    goto error;

  nbytes=ReadList(flist,PARAMLIST,Nparam);

  /* Read soilfile, - i.e. find BASIN.id and masl */
  status = ReadSoil(soilfilename,BASIN,IRRYEAR,south,north,west,east,irryearcells); 
  if (status != ENOERROR)
    goto error;

  /* Find main stem and dams */
  status = FindMainstemAndDams(DAM,BASIN,ndams);
  printf("Main stem and %d dams found\n\n",ndams);

  /* Allocate memory for CATCHMENT */
  CATCHMENT = (float**)calloc(ncells+1,sizeof(float*));
  for(i=0;i<=ncells;i++) 
    CATCHMENT[i]=(float*)calloc(5,sizeof(float));

  /* Go through all dams built for irrigation purposes.
     Need to go through them all here so that you, before calculating irrigation water demands,
       know all details on which dam is upstream/downstream of other dams, 
       mean annual inflow to each dam (this one isn't really used, but keep in in case you need it later on),
       and each dam's downstream river stem and cells being defined as "downstream" of each dam.
       You also need to figure out where to extract water from in routing program (i.e. upstream dams, 
       but not those located "directly" upstream current cell. */
  for(i=1;i<=ndams;i++) {
     printf("Dam %d %s\n",i,DAM[i].damname);
     if(DAM[i].irrdam==1) {
      printf("Dam %d is built for irrigation purposes. Find upstream cells (row %d col %d)\n",i,DAM[i].row,DAM[i].col);
      /* Find catchment upstream dam in question. Information stored in CATCHMENT */
      SearchCatchment(BASIN,CATCHMENT,rows,cols,DAM[i].row,DAM[i].col,&upstream_cells);
      /* Find mean annual inflow for the period 1980-1999 */
      status = ReadFluxes(indir,CATCHMENT,upstream_cells,&inflow,PARAMLIST,Nparam,nbytes,NDays,NYears);
      if (status != ENOERROR)
	goto error;
      DAM[i].mean_annual_inflow=inflow;
      /* Find cells located downstream current irrigation dam.  
         "Downstream" is defined as being at 
                    1. Elevation lower than current dam
                    2. Being closer to the stem (downstream current dam) than MAXDISTANCE 
                    3. Not being upstream another dam (regardless of purpose) in a tributary */
      status = FindCells(i,ndams,DAM,BASIN,CATCHMENT,RES,rows,cols,upstream_cells);
      if (status != ENOERROR)
	goto error;
     }
  }

  /* Go through all dams again, and revisit the cells with information gained in previous loop.
     Cells having another dam in between itself and the dam in question (i.e. a dam built for irrig puporses 
     on the mainstem) will be excluded. If you want the included, then comment out this section. */
  for(i=1;i<=ndams;i++) {
    if(DAM[i].irrdam==1) {
      status = FindCellsSecondTime(i,ndams,DAM,BASIN,CATCHMENT,RES,rows,cols,upstream_cells);
      if (status != ENOERROR)
	goto error;
    }
  }

  /* Calculate irrigation water demands for each dam and write output */
  status = WriteOutput(indir,outfilename2,outfilename3,
		       resdirname,DAM,BASIN,rows,cols,ndams,year,
		       Nparam,nbytes,PARAMLIST,StartYear,
		       NYears,NDays,PrecCol,EvapCol);
  if (status != ENOERROR)
    goto error;

  return 0;

  error:
  ProcessError();
  return 0;
} 
/******************************************************************************/
/*			       ProcessCommandLine                             */
/******************************************************************************/
int ProcessCommandLine(int argc,char **argv,int *year,char *dirfilename,
		       char *fracfilename,char *damfilename,char *soilfilename,
		       char *indir,char *outfilename2,char *outfilename3,
		       char *resdirname,char *irrfile,char *flist,
		       int *StartYear,int *EndYear,int *PrecCol,int *EvapCol)
{
  if (argc != 16) {
    status = EUSAGE;
    strcpy(message, argv[0]);
    goto error;
  }

  *year = atoi(argv[1]);
  strcpy(dirfilename, argv[2]);
  strcpy(fracfilename, argv[3]);
  strcpy(damfilename, argv[4]);
  strcpy(soilfilename, argv[5]);
  strcpy(indir, argv[6]);
  strcpy(outfilename2, argv[7]);
  strcpy(outfilename3, argv[8]);
  strcpy(resdirname, argv[9]);
  strcpy(irrfile, argv[10]);
  strcpy(flist, argv[11]);
  *StartYear = atoi(argv[12]);
  *EndYear = atoi(argv[13]);
  *PrecCol = atoi(argv[14]);
  *EvapCol = atoi(argv[15]);

  return EXIT_SUCCESS;
  
 error:
    return status;
}

/******************************************************************************/
/*				  Processerror                                */
/******************************************************************************/
int ProcessError(void)
{
  if (errno) 
    perror(message);
  else {
    switch (status) {
    case EUSAGE:
      fprintf(stderr, usage, message);
      break;
    case ENODOUBLE:
      fprintf(stderr, "\nNot a valid double: %s\n\n", message);
      break;
    case ENOFLOAT:
      fprintf(stderr, "\nNot a valid float:\n%s\n\n", message);
      break;      
    case ENOINT:
      fprintf(stderr, "\nNot a valid integer:\n%s\n\n", message);
      break;      
    case ENOLONG:
      fprintf(stderr, "\nNot a valid long: %s\n\n", message);
      break;
    case ENOSHORT:
      fprintf(stderr, "\nNot a valid short: %s\n\n", message);
      break;
    case EBASIN:
      fprintf(stderr, "\nBasin does not exist: %s\n\n",message);
      break;      
    case ESCAN:
      fprintf(stderr, "\nError scanning file: %s\n\n", message);
      break;
    default:
      fprintf(stderr, "\nError: %s\n\n", message);
      break;
    }
  }
  status = ENOERROR;
  return status;
}
/******************************************************************************/
/*				 FindRowsCols                                 */
/******************************************************************************/
int FindRowsCols(char *dirfilename,int *rows,int *cols,
		 float *south,float *west)
{
  FILE *dirfile = NULL;
  char filename[BUFSIZ+1];
  float cellsize;
  float nodata;

  /* Open input direction file */
  sprintf(filename,"%s",dirfilename);
  dirfile = fopen(filename, "r");
  if (dirfile == NULL) {
    status = errno;
    strcpy(message, filename);
    goto error;
  }
  //  else printf("Direction file opened: %s\n",filename);

  /* Read header */
  fscanf(dirfile,"%*s %d ",&(*cols));
  fscanf(dirfile,"%*s %d ",&(*rows));
  fscanf(dirfile,"%*s %f ",&(*west));
  fscanf(dirfile,"%*s %f ",&(*south));
  fscanf(dirfile,"%*s %f ",&cellsize);
  fscanf(dirfile,"%*s %f ",&nodata);

  fclose(dirfile);

  return ENOERROR;
 error:
  if (dirfile != NULL)
    fclose(dirfile);
  return status;
}
/******************************************************************************/
/*				 ReadDirFile                                  */
/******************************************************************************/
int ReadDirFile(char *dirfilename,CELL **BASIN,
		const float RES,int *ncells)
{
  FILE *dirfile = NULL;
  char filename[BUFSIZ+1];
  int i;
  int j;
  int rows,cols;
  float west;
  float south;
  float cellsize;
  float nodata;

  /* Open input fraction file */
  sprintf(filename,"%s",dirfilename);
  dirfile = fopen(filename, "r");
  if (dirfile == NULL) {
    status = errno;
    strcpy(message, filename);
    goto error;
  }
  //  else printf("Direction file opened: %s\n",filename);

  /* Read header */
  fscanf(dirfile,"%*s %d ",&cols);
  fscanf(dirfile,"%*s %d ",&rows);
  fscanf(dirfile,"%*s %f ",&west);
  fscanf(dirfile,"%*s %f ",&south);
  fscanf(dirfile,"%*s %f ",&cellsize);
  fscanf(dirfile,"%*s %f ",&nodata);

  (*ncells)=0;

  for (i = rows; i >= 1; i--) {
    for (j = 1; j <= cols; j++) {
       fscanf(dirfile,"%d ",&BASIN[i][j].direction);
       if(BASIN[i][j].direction>nodata) {
	 (*ncells)+=1;
       }
    }
  }

  fclose(dirfile);

  for(i=1;i<=rows;i++) {
    for(j=1;j<=cols;j++) {
      if(BASIN[i][j].direction==0 || BASIN[i][j].direction==nodata) {
	BASIN[i][j].tocol=0;
	BASIN[i][j].torow=0;
      } 
      else if(BASIN[i][j].direction==1) {
         BASIN[i][j].tocol=j;
         BASIN[i][j].torow=i+1;
      } 
      else if(BASIN[i][j].direction==2) {
         BASIN[i][j].tocol=j+1;
         BASIN[i][j].torow=i+1;
      } 
      else if(BASIN[i][j].direction==3) {
         BASIN[i][j].tocol=j+1;
         BASIN[i][j].torow=i;
      } 
      else if(BASIN[i][j].direction==4) {
         BASIN[i][j].tocol=j+1;
         BASIN[i][j].torow=i-1;
      } 
      else if(BASIN[i][j].direction==5) {
         BASIN[i][j].tocol=j;
         BASIN[i][j].torow=i-1;
      } 
      else if(BASIN[i][j].direction==6) {
         BASIN[i][j].tocol=j-1;
         BASIN[i][j].torow=i-1;
      } 
      else if(BASIN[i][j].direction==7) {
         BASIN[i][j].tocol=j-1;
         BASIN[i][j].torow=i;
      } 
      else if(BASIN[i][j].direction==8) {
         BASIN[i][j].tocol=j-1;
         BASIN[i][j].torow=i+1;
      } 
    }
  }

  return ENOERROR;
 error:
  if (dirfile != NULL)
    fclose(dirfile);
  return status;
}
/******************************************************************************/
/*				 ReadFracFile                                 */
/******************************************************************************/
int ReadFracFile(char *fracfilename,CELL **BASIN,
		const float RES)
{
  FILE *fracfile = NULL;
  char filename[BUFSIZ+1];
  int i;
  int j;
  int rows,cols;
  float west;
  float south;
  float cellsize;
  float nodata;

  /* Open input fraction file */
  sprintf(filename,"%s",fracfilename);
  fracfile = fopen(filename, "r");
  if (fracfile == NULL) {
    status = errno;
    strcpy(message, filename);
    goto error;
  }
  //  else printf("Fraction file opened: %s\n",filename);

  /* Read header */
  fscanf(fracfile,"%*s %d ",&cols);
  fscanf(fracfile,"%*s %d ",&rows);
  fscanf(fracfile,"%*s %f ",&west);
  fscanf(fracfile,"%*s %f ",&south);
  fscanf(fracfile,"%*s %f ",&cellsize);
  fscanf(fracfile,"%*s %f ",&nodata);

  for (i = rows; i >= 1; i--) {
    for (j = 1; j <= cols; j++) {
       fscanf(fracfile,"%f ",&BASIN[i][j].fraction);
    }
  }

  fclose(fracfile);
  return ENOERROR;

 error:
  if (fracfile != NULL)
    fclose(fracfile);
  return status;
}
/******************************************************************************/
/*				 ReadDams                                     */
/******************************************************************************/
int ReadDams(char *dam_file,int ndams,LIST *DAM,int *irrdams)
{
  FILE *damfile = NULL;
  char filename[BUFSIZ+1];
  int p1,p2,p3; //integer
  int i,j;

  /* Open dam information file */
  sprintf(filename,"%s",dam_file);
  damfile = fopen(filename, "r");
  if (damfile == NULL) {
    status = errno;
    strcpy(message, filename);
    goto error;
  }
  //else printf("DamFile opened: %s\n",dam_file);

  for(i=0;i<=ndams;i++) {
	DAM[i].col=0;
	DAM[i].row=0;
	DAM[i].id=0;
	DAM[i].lat=0;
	DAM[i].lon=0;
	DAM[i].year=0;
	DAM[i].capacity=0;
	DAM[i].area=0;
	DAM[i].mean_annual_inflow=0.;
	DAM[i].irrareas=0.;
	DAM[i].height=0.;
	DAM[i].irrdam=0;
  }

  /* Read damfile */
  j=0;
  for(i = 0; i < ndams; i++) {
    p1=p2=p3=0;
    fscanf(damfile,"%d %d %f %f %d ",
	   &DAM[i+1].col,&DAM[i+1].row,&DAM[i+1].lon,&DAM[i+1].lat,&DAM[i+1].id);
    fscanf(damfile,"%s %d %f %f ",
	   DAM[i+1].damname,&DAM[i+1].year,&DAM[i+1].capacity,&DAM[i+1].area);
    fscanf(damfile,"%f %d %d ",
	   &DAM[i+1].catcharea,&DAM[i+1].instcap,&DAM[i+1].annenergy);
    fscanf(damfile,"%d %d %s ",
	   &DAM[i+1].irrareas,&DAM[i+1].height,DAM[i+1].purpose);
    p1=DAM[i+1].purpose[0];
    p2=DAM[i+1].purpose[1];
    p3=DAM[i+1].purpose[2];
    if(p1==73 || p2==73 || p3==73) {
      j+=1;
      DAM[i+1].irrdam=1;
    }
  }
  printf("Dams having purpose = irrigation: %d\n",j);

  (*irrdams)=j;

  return ENOERROR;
 error:
  if (damfile != NULL)
    fclose(damfile);
  return status;
}
/******************************************************************************/
/*				 FindCells                                    */
/******************************************************************************/
int FindCells(int irrdam,int ndams,LIST *DAM,CELL **BASIN,float **CATCHMENT,const float RES,
	      int rows,int cols,int upstream_cells)
{
  int i,j,k;
  int irow,icol,upstream;
  int dam,dam_e;  //dam: dam_e:
  int count,count_e;
  int conditions_met=0;
  int split;

  /* Find downstream cells for each dam */
  for (i = rows; i >= 1; i--) {
    for (j = 1; j <= cols; j++) {
      if(BASIN[i][j].direction>=0) {
        upstream=0;
	dam=dam_e=0;
	count=BASIN[i][j].upstream_irrdams;
	count_e=BASIN[i][j].extract_irrdams;
	irow=DAM[irrdam].row;
	icol=DAM[irrdam].col;
	for(k=0;k<upstream_cells;k++) { /* First locate cells upstream current dam */
	  if(fabs(BASIN[i][j].lat-CATCHMENT[k][0])<EPS && 
	     fabs(BASIN[i][j].lon-CATCHMENT[k][1])<EPS) {
	    upstream=TRUE;
	  }
	}
        /* Is current cell located at a lower elevation, and not upstream the dam? */
	if(BASIN[i][j].masl<=BASIN[irow][icol].masl && upstream!=TRUE) { 
          /* Check also if other conditions are met + figure out whether demand should be extracted
	   directly or not */
          conditions_met=CheckIfConditionsMet(DAM,BASIN,irrdam,ndams,i,j,&split); 
	  if(conditions_met) { /* i.e. not upstream another dam */
	      BASIN[i][j].dam[count+1]=irrdam;
	      dam=1;
	      }
	    if(conditions_met && split==1) { /* i.e not upstream another dam,
						and not directly downstream dam in question */
	      BASIN[i][j].extractdam[count_e+1]=irrdam;
	      dam_e=1;
	    }

	}
	BASIN[i][j].upstream_irrdams+=dam;
	BASIN[i][j].extract_irrdams+=dam_e;
     }
    }
  }
  
  return ENOERROR;

}
/******************************************************************************/
/*				 FindCellsSecondTime                          */
/******************************************************************************/
int FindCellsSecondTime(int irrdam,int ndams,LIST *DAM,CELL **BASIN,float **CATCHMENT,
			const float RES,int rows,int cols,int upstream_cells)
{
  int i,j,k,l,m,n,o;
  int irow,icol;
  int flag[MAXDAMS+1],flag2,count;

  /* Find downstream cells for each dam */
  for (i = rows; i >= 1; i--) {
    for (j = 1; j <= cols; j++) {
      for(k=1;k<=MAXDAMS;k++) 
	flag[k]=0;
      for(k=1;k<=BASIN[i][j].extract_irrdams;k++) {
	l=BASIN[i][j].extractdam[k];
	if(l==irrdam) {
	  for(m=1;m<=DAM[l].mainstem_length;m++) {
	    irow=DAM[l].MAINSTEM[m].row;
	    icol=DAM[l].MAINSTEM[m].col;
	    for(n=1;n<=BASIN[i][j].extract_irrdams;n++) {
	      o=BASIN[i][j].extractdam[n];
	      if(irow==DAM[o].row && icol==DAM[o].col) {
		/* ta bort denne dammen!!!*/
		flag[irrdam]=1;
	      }
	    }
	  }
	}
      }
      flag2=0;
      count=0;
      for(k=1;k<=BASIN[i][j].extract_irrdams;k++) {
	if(count<BASIN[i][j].extract_irrdams) {
	  count+=1;
	  l=BASIN[i][j].extractdam[k];
	  if(l==irrdam && flag[irrdam]==1 && flag2==0) {
	    BASIN[i][j].extract_irrdams-=1;
	    flag2=1;
	    count+=1;
	  }
	    BASIN[i][j].extractdam[k]=BASIN[i][j].extractdam[count];
	}
      }
     } /* for j */
  } /* for i */

  return ENOERROR;

}
/******************************************************************************/
/*				  SetToMissing                                */
/******************************************************************************/
int SetToMissing(int format, void *value)
{
  switch (format) {
  case double_f:
    *((double *)value) = MISSING;
    break;
  case int_f:
    *((int *)value) = MISSING;
    break;
  case float_f:
    *((float *)value) = MISSING;
    break;
  case long_f:
    *((long *)value) = MISSING;
    break;
  case short_f:
    *((short *)value) = MISSING;
    break;
  default:
    strcpy(message, "Unknown number format");
    status = ENOCASE;
    goto error;
    break;
  }    
  return ENOERROR;
  
 error:
  return status;
}
/********************************************/
/* Function returns number of lines in file */
/********************************************/
int LineCount(const char *file)
{
  FILE *fp;
  int c, lines;

  if((fp = fopen(file,"r"))==NULL){
    printf("Cannot open file %s, exiting \n",file);exit(0);}

  lines = 0;

  while((c = fgetc(fp)) !=EOF)  if (c=='\n') lines++;
  fclose(fp);

  return lines;
}/* END function int line_count(char *file)   */

/******************************************************************************/
/*			       ReadSoil                                       */
/******************************************************************************/
int ReadSoil(char *infilename,CELL **BASIN,float **IRRYEAR,
	     float south,float north,float west,float east,
	     int irryearcells)
{
  FILE *infile = NULL;
  char LINE[BUFSIZ+1];
  int soilcells;
  int i,j,icol,irow,soilid;
  float latitude,longitude,soilmasl;
  int maslcol=22;

  infile = fopen(infilename, "r");
  if (infile == NULL) {
    status = errno;
    strcpy(message, infilename);
    goto error;
  }
  else printf("Soilfile opened: %s\n",infilename);

  /* Read infile (soil) */
  soilcells = LineCount(infilename);
  for(i=0;i<soilcells;i++) {
    fscanf(infile,"%*d %d %f %f ",&soilid,&latitude,&longitude);
    for(j=5;j<maslcol;j++) 
      fscanf(infile,"%*f ");
    fscanf(infile,"%f ",&soilmasl);
    fgets(LINE,500,infile);   
     if(latitude<=north && latitude>=south && longitude>=west && longitude<=east ) {
      irow=(int)((latitude-south+RES/2.)/RES);
      icol=(int)((longitude-west+RES/2.)/RES);
      if(BASIN[irow][icol].direction>=0) {
	BASIN[irow][icol].masl=soilmasl;
	BASIN[irow][icol].id=soilid;
	BASIN[irow][icol].irrfraction=FindIrr(IRRYEAR,latitude,longitude,irryearcells);	
      }
    }
  }

  fclose(infile);
  return ENOERROR;

 error:
  return status;
}
/*********************************************/
/* FindMainstemAndDams                       */
/*********************************************/
int FindMainstemAndDams(LIST *DAM, CELL **BASIN,int ndams)
{
  int i,j,k,irow,icol,count,direction;
  int nextrow[9] = { 0, 1, 1, 0, -1, -1, -1, 0, 1 };
  int nextcol[9] = { 0, 0, 1, 1, 1, 0, -1, -1, -1 };

  /* Find main stem */
  for(i=1;i<=ndams;i++) {
    count=0;
    irow = DAM[i].row;
    icol = DAM[i].col; 
    direction=BASIN[irow][icol].direction; 
    DAM[i].masl=BASIN[irow][icol].masl; 

    while(direction>0) {
      DAM[i].MAINSTEM[count].lat=BASIN[irow][icol].lat;
      DAM[i].MAINSTEM[count].lon=BASIN[irow][icol].lon;
      DAM[i].MAINSTEM[count].row=irow;
      DAM[i].MAINSTEM[count].col=icol;
      direction=BASIN[irow][icol].direction;
      irow=irow+nextrow[direction];
      icol=icol+nextcol[direction];
      count+=1;
    }
    DAM[i].mainstem_length=count;    
  }

  /* Find which other dams are in each dam's main stem */
  for(i=1;i<=ndams;i++) {
    for(k=1;k<=ndams;k++) {
      DAM[i].mainstemdam[k]=0;
      for(j=0;j<DAM[i].mainstem_length;j++) { 
	irow=DAM[i].MAINSTEM[j].row;
	icol=DAM[i].MAINSTEM[j].col;
	if(irow==DAM[k].row && icol==DAM[k].col && DAM[i].irrdam==1 && DAM[k].irrdam==1) 
	  DAM[i].mainstemdam[k]=1;
      }
    }
  }

  return ENOERROR;
}
/*********************************************/
/* CheckIfConditionsMet                      */
/*********************************************/
int CheckIfConditionsMet(LIST *DAM, CELL **BASIN,int irrdam,
			 int ndams,int therow,int thecol,int *split)
{
  int i,j,k,count,direction;
  int nextrow[9] = { 0, 1, 1, 0, -1, -1, -1, 0, 1 };
  int nextcol[9] = { 0, 0, 1, 1, 1, 0, -1, -1, -1 };

  int ok;
  int irow,icol;

 /* Here: therow,thecol = cell currently being checked to see if it meets the 
conditions to extract water for dam #irrdam (which is built for irrig purposes).
It is already known that therow/thecol is at lower elevation, and not upstream 
location of irrdam. Here location of cell comp to other dams in the basin will be checked, and if the 
cell is directly downstream of irrdam, or too far (>maxdistance) from downstream stem. */

  direction=BASIN[therow][thecol].direction; 
  count=0; /*count=0, used to keep track of distance to mainstem */
  ok=1; /* initial assumption: cell is ok */
  (*split)=1; /*split=1 means water can be extracted from irrdam*/ 
  irow=therow; 
  icol=thecol;

  /* Check if current cell is upstream another dam (regardless of purpose) 
     Check also how far to dam's main river stem. */
  while(direction!=NODIRECTION && direction>0) { 

    for(i=0;i<DAM[irrdam].mainstem_length;i++) {
      for(j=1;j<=ndams;j++) { /* Check if current cell is upstream another dam */
	if(irow==DAM[j].row && icol==DAM[j].col) {
	  /* Check if this other dam is located on the downstream river stem 
	     of the dam in question, if so this cell should be included after all. 
             Will test later whether there is a dam in between current cell and irrdam */
	  for(k=0;k<DAM[irrdam].mainstem_length;k++)   
	    if(irow!=DAM[irrdam].MAINSTEM[i].row && icol!=DAM[irrdam].MAINSTEM[i].col) { 
	      ok=0; //current cell is located upstream another dam in a "tributary"
	      count=1; //to make sure not included again in "the end"
	      goto the_end;
	    }
	}
      }
      /* No dam hindrance encountered yet, so check if you end up in the river stem downstream of
         dam in question. If so, the cell investigated should be included when calculating water
	 demands. (although there's an additional test below... ) */
      if(irow==DAM[irrdam].MAINSTEM[i].row && icol==DAM[irrdam].MAINSTEM[i].col) {
	ok=1;
	goto the_end;
      }
    } /* for i=0;i<mainstem_length */

    irow=irow+nextrow[direction];
    icol=icol+nextcol[direction];
    direction=BASIN[irow][icol].direction;
    count+=1;

    /* Check how many cells you have traversed. 
       If more than MAXDISTANCE (currently: 5) cells from dam's river stem, 
       the cell investigated should not be included. */
    if(count>=MAXDISTANCE) {
      ok=0;
      goto the_end;
    }

  } // while direction>0


 the_end:
  if(count==0) {
    ok=1;
    (*split)=0; /*cell located directly downstream the dam,
                  and water should therefore be extracted locally
		  and not from the dam directly, although other criterias are met */
  }

  return ok;
}
/*********************************************/
/* SearchCatchment                           */
/* Purpose: Find cells upstream              */ 
/*          current point location           */
/*********************************************/
int SearchCatchment(CELL **BASIN,float **CATCHMENT,int nrows,int ncols,
		    int irow,int icol,int *upstream_cells)
{
  int i,j;
  int ii,jj,iii,jjj;
  int count;
 
  count = 0;
  (*upstream_cells)=0;

  for(i=1;i<=nrows;i++) {
    for(j=1;j<=ncols;j++) {
      ii=i;
      jj=j;
    loop:
      if(ii>nrows || ii<1 || jj>ncols || jj<1) {
	//printf("Outside basin %d %d %d %d\t",ii,jj,nrows,ncols);
      }
      else {
	if(ii==irow && jj==icol) { 
	  count+=1;
	  CATCHMENT[count][0]=BASIN[i][j].lat;
	  CATCHMENT[count][1]=BASIN[i][j].lon;
	  CATCHMENT[count][2]=BASIN[i][j].fraction;
	}
	else { 
	  /*check if the current ii,jj cell routes down
	    to the subbasin outlet point, following the
	    flow direction from each cell;
	    if you get here no_of_cells increment and you 
	    try another cell*/ 
	  if(BASIN[ii][jj].tocol!=0 &&    
	     BASIN[ii][jj].torow!=0) { 
	    iii = BASIN[ii][jj].torow;         
	    jjj = BASIN[ii][jj].tocol;         
	    ii  = iii;                  
	    jj  = jjj;                  
	    goto loop;
	  }
	}
      }
    }
  }

  (*upstream_cells)=count;
  //printf("Upstream grid cells from present dam: %d\n", 
	 (*upstream_cells);
//printf("Dam is located at row:%d col:%d\n",irow,icol);

	 
  return ENOERROR;

}

/****************************************/
/* ReadFluxes:                         */
/*****************************************/
int ReadFluxes(char *indir,float **CATCHMENT,
	       int upstream_cells,float *inflow,
	       int **PARAMLIST,int Nparam,int nbytes,
	       int NDays,int NYears)
{
  FILE *fp;
  int i,j,k;
  char LATLON[60];
  char file[400];
  char fmtstr[150];
  char tmpdata[100];
  char *cptr;
  short int *siptr;
  unsigned short int *usiptr;
  int   *iptr;
  float *fptr;
  float runoff;
  float area;
  int skip;
  int days;

  skip = NSKIP;
  days = NDays;

  /* Allocate */
  cptr = (char *)malloc(1*sizeof(char));
  usiptr = (unsigned short int *)malloc(1*sizeof(unsigned short int));
  siptr = (short int *)malloc(1*sizeof(short int));
  iptr = (int *)malloc(1*sizeof(int));
  fptr = (float *)malloc(1*sizeof(float));

  runoff=0;

  for(j=1;j<=upstream_cells;j++) {
      area=CalcArea(CATCHMENT[j][0]);
      /* Make filename */
      sprintf(fmtstr,"/noirrig.wb.24hr/fluxes_%%.%if_%%.%if",DECIMAL_PLACES,DECIMAL_PLACES);
      sprintf(LATLON,fmtstr,CATCHMENT[j][0],CATCHMENT[j][1]);
      strcpy(file,indir);
      strcat(file,LATLON);
      if((fp = fopen(file,"rb"))==NULL) { 
	  printf("Cannot open file (ReadFLuxes) %s, exiting\n",file);
	  exit(0); 
      }
      // else printf("File opened: %s\n",file);
      
   /* Skip data */
    for(i=0;i<skip;i++) {
      fread(tmpdata,nbytes,sizeof(char),fp);
    }
    
      /* Read data */
      for(i=0;i<days;i++) {
	  for(k=0;k<Nparam;k++) {
	      if(PARAMLIST[k][3]==1)  
		  fread(cptr,1,sizeof(char),fp);
	      if(PARAMLIST[k][3]==2) 
		  fread(usiptr,1,sizeof(unsigned short int),fp);
	      if(PARAMLIST[k][3]==3) 
		  fread(siptr,1,sizeof(short int),fp);
	      if(PARAMLIST[k][3]==4) {
		  fread(fptr,1,sizeof(float),fp);
		  if(k==7 || k==8) runoff += fptr[0]*area*CATCHMENT[j][2]; //i.e. volume water in units of 1000 m3
	      }
	      if(PARAMLIST[k][3]==5) {
		  fread(iptr,1,sizeof(int),fp);
	      }
	  }
      }
      fclose(fp); 
  }

  (*inflow)=runoff/((float)(NYears-skip/365.)); //units 1000 m3 pr day
  
  return ENOERROR;
}
/**********************************************************************/
/*			   CalcArea                                   */
/**********************************************************************/
float CalcArea(float lat)	/* latitude of gridcell center in degrees */
	       
{
  float area;
  float res;

  lat *= DEGTORAD;
  res = RES*DEGTORAD/2.;
  area = res * EARTHRADIUS * EARTHRADIUS * 
    fabs(sin(lat+res)-sin(lat-res));
  

  return area;
}
/*****************************************************************/
/*                   WriteOutput                                 */
/*****************************************************************/
int WriteOutput(char *indir,char *outfilename2,char *outfilename3,
		char *resdirname,LIST *DAM,CELL **BASIN,int rows,int cols,
		int ndams,int simyear,int Nparam,int nbytes,int **PARAMLIST,
		int StartYear,int NYears,int NDays,int PrecCol,int EvapCol)
{
  FILE *outfile2 = NULL;
  FILE *outfile3 = NULL;
  FILE *resfile = NULL;
  int i,j,k,l,year,month;
  int checkdam;
  float **IRR_DEMAND, **GROSSIRR_DEMAND, *MONTH_DEMAND;
  float Fraction_inv[10],Fraction;
  char filename[BUFSIZ+1];

 /* Open outfile used by plotting program */
  outfile2 = fopen(outfilename2, "w");
  if (outfile2 == NULL) {
    status = errno;
    strcpy(message, outfilename2);
    goto error;
  }
  else printf("outfile2 opened (*.upstream, only used for plotting purposes): %s\n",outfilename2);

 /* Open outfile used by metdata.modify.met_irr.new.c 
  and routing.subtractwaterusedforirrigation.new.c*/
  outfile3 = fopen(outfilename3, "w");
  if (outfile3 == NULL) {
    status = errno;
    strcpy(message, outfilename3);
    goto error;
  }
  else printf("outfile3 opened (*.extractwater, for metdata.modify* and routing.subtract*): %s\n",outfilename3);

  /* Print to files 2 and 3 */
  for (i = rows; i >= 1; i--) {
    for (j = 1; j <= cols; j++) {
       if(BASIN[i][j].direction>=0) {

	fprintf(outfile2,"%d %.4f %.4f %d\t",    // res Tian 2013
	       BASIN[i][j].id,BASIN[i][j].lat,BASIN[i][j].lon,BASIN[i][j].upstream_irrdams);
	for(k=1;k<=BASIN[i][j].upstream_irrdams;k++) {
          l=BASIN[i][j].dam[k];
	  fprintf(outfile2,"%d %.4f %.4f %.4f %.4f ",   // res Tian 2013
		 BASIN[i][j].dam[k],DAM[l].lat,DAM[l].lon,DAM[l].capacity,DAM[l].mean_annual_inflow);
	}
	fprintf(outfile2,"\n");

	fprintf(outfile3,"%d %.4f %.4f %d\t",  // res Tian 2013
	       BASIN[i][j].id,BASIN[i][j].lat,BASIN[i][j].lon,BASIN[i][j].extract_irrdams);
	for(k=1;k<=BASIN[i][j].extract_irrdams;k++) {
          l=BASIN[i][j].extractdam[k];
	  fprintf(outfile3,"%d %.4f %.4f %.4f %.4f ",  // res Tian 2013
		 BASIN[i][j].extractdam[k],DAM[l].lat,DAM[l].lon,DAM[l].capacity,DAM[l].mean_annual_inflow);
	}
	fprintf(outfile3,"\n");
       }
    }
  }
  fclose(outfile2);
  fclose(outfile3);

  IRR_DEMAND = (float**)calloc(NYears+1,sizeof(float*));
  for(i=0;i<=NYears+1;i++) 
    IRR_DEMAND[i]=(float*)calloc(13,sizeof(float));
  GROSSIRR_DEMAND = (float**)calloc(NYears+1,sizeof(float*));
  for(i=0;i<=NYears+1;i++) 
    GROSSIRR_DEMAND[i]=(float*)calloc(13,sizeof(float));
  MONTH_DEMAND = (float*)calloc(13,sizeof(float));

  /* Calculate irrigation water demand for each dam built for irrigation purposes */
  printf("Calculate irrigation water demand for %d dams\n",ndams);
  for(k=1;k<=ndams;k++) {
    for(year=0;year<=NYears;year++) {
      for(month=0;month<=12;month++) {
	IRR_DEMAND[year][month]=0.;
	GROSSIRR_DEMAND[year][month]=0.;
        MONTH_DEMAND[month]=0.;
      }
    }
    printf("Dam number %d %d %s masl:%d lat%.4f lon%.4f\n",k,DAM[k].irrdam,DAM[k].damname,DAM[k].masl,DAM[k].lat,DAM[k].lon);
    if(DAM[k].irrdam==1) {
      printf("Dam number %d is an irrdam\n",k);
      for (i = rows; i >= 1; i--) {
	for (j = 1; j <= cols; j++) {
	  if(BASIN[i][j].direction>0 && BASIN[i][j].irrfraction>IRRCUTLEVEL) {
	    Fraction_inv[0]=1.;
	    for(l=1;l<=BASIN[i][j].upstream_irrdams;l++) { //Check all irrdams upstream current cell
	      checkdam=BASIN[i][j].dam[l];
	      if(DAM[k].mainstemdam[checkdam]==1 || DAM[checkdam].mainstemdam[k]==1 ) { // dams in series 
	      }
	      else { //parallell dams
		if(DAM[checkdam].irrdam==1 && k!=checkdam) { //parallell irrdams
		  Fraction_inv[0] += (float)DAM[checkdam].capacity/(float)DAM[k].capacity;
		}
	      }
	    }
	    Fraction=1/Fraction_inv[0]; 
	    for(l=1;l<=BASIN[i][j].upstream_irrdams;l++) { //Calculate water demand
	      if(k==BASIN[i][j].dam[l]) {
		CalculateWaterDemand(indir,IRR_DEMAND,GROSSIRR_DEMAND,BASIN[i][j].lat,BASIN[i][j].lon,
				     BASIN[i][j].fraction,Fraction,Nparam,nbytes,PARAMLIST,StartYear,
				     NDays,PrecCol,EvapCol);
	      }
	    }
	  }
	}
      }
    }
    else {
      printf("not built for irrigation purposes\n");
    }

    /* Write to file which will give a time series of monthly irrigation 
       demands for the dam in question. This file will be used by the routing program. */
    sprintf(filename,"%s/%s.calc.irrdemand.monthly",
	    resdirname,DAM[k].damname);
    resfile = fopen(filename, "w");
    if (resfile == NULL) {
      printf("Resfile not found: %s\n",filename);
      exit(0);
    }
    else printf("File giving monthly time series of irrigation water demands opened: %s\n",filename);
    
    for(year=0;year<NYears;year++) {
      for(month=1;month<=12;month++) {
	if(IRR_DEMAND[year][month]<0) IRR_DEMAND[year][month]=0.;
	MONTH_DEMAND[month]+=IRR_DEMAND[year][month];	
	fprintf(resfile,"%d %d %.3f %.3f\n",
		year+StartYear,month,IRR_DEMAND[year][month]/(30.*24.*3.6),GROSSIRR_DEMAND[year][month]/(30.*24.*3.6)); // m3s-1, assumes 30 days pr month
      }
    }
    fclose(resfile);
    printf("Results written to file: %s\n",filename);
  }
 
  return 0;
  
 error:
  return status;
  
}
/*****************************************************************/
/*             CalculateWaterDemand                              */
/*****************************************************************/
void CalculateWaterDemand(char *indir,float **IRR_DEMAND,float **GROSSIRR_DEMAND, 
			  float lat,float lon,
			  float fraction,float demandfraction,
			  int Nparam,int nbytes,int **PARAMLIST,
			  int StartYear,int NDays,int PrecCol,int EvapCol)
{
  FILE *fp,*fp2;
  int i,k;
  char LATLON[60];
  char file[400];
  char file2[400]; 
  char fmtstr[150];
  char tmpdata[100];
  char indir1[400];
  char indir2[400];
  const char *postfix="freeirrig.wb.24hr";
  const char *postfix2="noirrig.wb.24hr";
  char *cptr;
  short int *siptr;
  unsigned short int *usiptr;
  int   *iptr;
  float *fptr;
  float area;
  int year,month;
		
  /* Allocate */
  cptr = (char *)malloc(1*sizeof(char));
  usiptr = (unsigned short int *)malloc(1*sizeof(unsigned short int));
  siptr = (short int *)malloc(1*sizeof(short int));
  iptr = (int *)malloc(1*sizeof(int));
  fptr = (float *)malloc(1*sizeof(float));

  area=CalcArea(lat); //km2


  /* Make filename */
  sprintf(indir1,"%s/%s",indir,postfix);
  sprintf(indir2,"%s/%s",indir,postfix2);
  sprintf(fmtstr,"/fluxes_%%.%if_%%.%if",DECIMAL_PLACES,DECIMAL_PLACES);
  sprintf(LATLON,fmtstr,lat,lon);
  strcpy(file,indir1);
  strcpy(file2,indir2);
  strcat(file,LATLON);
  strcat(file2,LATLON);
  if((fp = fopen(file,"rb"))==NULL) { 
    printf("Cannot open file (CalculateWaterDemand) %s, exiting\n",file);
    exit(0); 
  }
  //else printf("File opened: %s\n",file);
  if((fp2 = fopen(file2,"rb"))==NULL) { 
    printf("Cannot open file (CalculateWaterDemand) %s, exiting\n",file2);
    exit(0); 
  }
  //else printf("File opened: %s\n",file2);
  

  /* Skip data */
  for(i=0;i<NSKIP;i++) {
      fread(tmpdata,nbytes,sizeof(char),fp);
      fread(tmpdata,nbytes,sizeof(char),fp2);
  }
  
  /* Read data */
    for(i=0;i<NDays;i++) {
       for(k=0;k<Nparam;k++) {

      if(PARAMLIST[k][3]==1) { 
	fread(cptr,1,sizeof(char),fp);
	fread(cptr,1,sizeof(char),fp2);
      }
      if(PARAMLIST[k][3]==2) { 
	fread(usiptr,1,sizeof(unsigned short int),fp);
	fread(usiptr,1,sizeof(unsigned short int),fp2);
      }
      if(PARAMLIST[k][3]==3) {
	fread(siptr,1,sizeof(short int),fp);
	fread(siptr,1,sizeof(short int),fp2);
      }
      if(PARAMLIST[k][3]==4) {
	fread(fptr,1,sizeof(float),fp);

	if(k==PrecCol) {
        //printf("prec is %f\n",fptr);
        GROSSIRR_DEMAND[year][month] += fptr[0]*area*fraction*demandfraction; //+prec from freeirrg
        //printf("GD of %d year and %d month is %f\n",year,month,GROSSIRR_DEMAND[year][month]);
        }

	if(k==EvapCol) {
       // printf("evap is %f\n",fptr);
        IRR_DEMAND[year][month] += fptr[0]*area*fraction*demandfraction; //+evap from freeirrg
       // printf("ID of %d year and %d month is %f\n",year,month,IRR_DEMAND[year][month]);
        }

	fread(fptr,1,sizeof(float),fp2);

	if(k==PrecCol) GROSSIRR_DEMAND[year][month] -= fptr[0]*area*fraction*demandfraction; //-prec from noirrg
	if(k==EvapCol) IRR_DEMAND[year][month] -= fptr[0]*area*fraction*demandfraction; //-evap from noirrg
      }
      if(PARAMLIST[k][3]==5) { 
	fread(iptr,1,sizeof(int),fp);
	fread(iptr,1,sizeof(int),fp2);
      }

      if(k==0) year=iptr[0]-StartYear;
      if(k==1) month=iptr[0];
    }
  }
  
  fclose(fp);
  fclose(fp2); 
}
/*******************************************/
/* ReadIrr                                 */
/*******************************************/
void ReadIrr(char *irr,float **IRRYEAR,int cells)
{
  FILE *fp;
  int i;

  if((fp = fopen(irr,"r"))==NULL){
    printf("Cannot open file %s, exiting \n",irr);exit(0);}
  else printf("IrrFile opened %s\n",irr);

  for(i=0;i<cells;i++) 
    fscanf(fp,"%f %f %f",&IRRYEAR[i][0],&IRRYEAR[i][1],&IRRYEAR[i][2]);
  fclose(fp);

}/* END function */
/*******************************************/
/*Find irrfraction within cell           */
/*******************************************/
float FindIrr(float **IRRYEAR,float lat,float lon,int cells)
{
  float irr;
  int i;

  irr=0;

  for(i=0;i<cells;i++) { 
    if(IRRYEAR[i][0]==lat && IRRYEAR[i][1]==lon) { 
      irr=IRRYEAR[i][2];
      break;
    }
  }

  return irr;

}/* END function */

/*******************************************/
/*Read parameter list                      */
/********************************************/
int ReadList(char *flist,int **LIST,int Nparam)
{
  FILE *fp;
  int i;
  int nbytes;

  if((fp = fopen(flist,"r"))==NULL){
    printf("Cannot open file %s, exiting \n",flist);
    exit(0);
  }

  nbytes=0;

  for(i=0;i<Nparam;i++) {
      fscanf(fp,"%*s %d %d %d %d",&LIST[i][1],&LIST[i][2],&LIST[i][3],&LIST[i][4]);
      if(LIST[i][3]!=3 && LIST[i][3]!=5) nbytes+=LIST[i][3];
      if(LIST[i][3]==3) nbytes+=2;
      if(LIST[i][3]==5) nbytes+=4;     
  }
  fclose(fp);

  return nbytes;
}/* END function */
/***************************/
/*IsLeapYear               */
/***************************/
int IsLeapYear(int year) 
{
  if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) 
    return TRUE;
  return FALSE;
}
