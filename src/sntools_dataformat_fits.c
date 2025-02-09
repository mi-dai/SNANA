/*************************************************
  May 2011, R.Kessler


  Code to write and read SNANA data files in fits format.
  Handles data and simulated SNe.

  For a given sample there are two fits files;
  one for the header info and another for the
  light curves. The separation into two files
  is needed since fits files must be accessed
  sequentially.

  For simulations, the processing time is the same as 
  for the text-file options, but the disk-usage is
  45% compared to the TERSE-ASCII format, and 11%
  compared to the VERBOSE-ASCII format.


     HISTORY

  Feb 17 2017: write & read SUBSURVEY if SUBSURVEY != SURVEY

  Aug 25 2017: in wr_snfitsio_fillTable(), abort on any blank string.

  Sep 08 2017: write LCLIB params (analogous to SIMSED params)

  Jan 5 2018: add VPEC and VPEC_ERR
 
  Feb 7 2018: add new photflag_open argument to rd_snfitsio_open(),
              so that only header file is opened during init stage
              to count events.

  Jul 31 2018: column name change: SIM_NON1a -> SIM_TEMPLATE_INDEX

  Apr 2019: update to unpack and read spectra from sim-SPECTROGRAPH.
            Should work on real data too if spectra are packed
            in SPEC.FITS files.

  Jul 2019: add strong lens info.

  Oct 2020: read SNANA_VERSION from header and convert to float
       (enable analysis options based on snana_version in fits header)

**************************************************/

#include "fitsio.h"
#include "sntools.h"
#include "sntools_dataformat_fits.h"
#include "sntools_host.h" 
#include "sntools_trigger.h" 
#include "sntools_spectrograph.h"

// ==================================
void WR_SNFITSIO_INIT(char *path, char *version, char *prefix, int simFlag, 
		      int Nsubsample_mark,
		      char *headFile  // ==> return arg
		   ) {

  // May 2011 R.Kessler
  //
  // init HEAD and PHOT files using fitsio utility.
  // (I) *path is the path to the data (or sim)
  // (I) *version is the photometry version name
  // (I) *prefix  is the prefix for the fits filenames
  // (I) simFlag indicates if this is simulation or data
  // (O) headFile is the full name of the header file.
  //
  // Dec 1, 2011: add *prefix input argument.
  //
  // Aug 6 2014: check new simFlag options:
  //      SNFITSIO_SIMFLAG_SNANA and SNFITSIO_SIMFLAG_MAGOBS 
  //
  // Aug 2 2016: check for SPECTROGRAPH
  // Jun 14 2017: pass Nsubsample_mark as argument
  // Mar 18 2018: check for SNRMON
  // Aug 19 2019: check length of filename.
  // May 14 2020: set SNFITSIO_DATAFLAG
  // Sep 10 2020: begin refactor with BYOSED -> PySEDMODEL

  int  itype, ipar, OVP, lenpath, lenfile, lentot ;
  char *ptrFile, *ptrFile2, *ptrType ;
  char fnam[] = "WR_SNFITSIO_INIT"  ;
  
  // --------------- BEGIN --------------

  // set global logical for SIM
  SNFITSIO_DATAFLAG             = false ;
  SNFITSIO_SIMFLAG_SNANA        = false ;
  SNFITSIO_SIMFLAG_MAGOBS       = false ; 

  SNFITSIO_SIMFLAG_SPECTROGRAPH = false ;
  SNFITSIO_SIMFLAG_SNRMON       = false ;
  SNFITSIO_SIMFLAG_MODELPAR     = false ;
  SNFITSIO_SIMFLAG_NBR_LIST     = false ; 
  SNFITSIO_COMPACT_FLAG         = false ; 

  OVP = ( simFlag & WRITE_MASK_SIM_SNANA) ;
  if ( OVP > 0 )  {// full SNANA sim
    SNFITSIO_SIMFLAG_SNANA = true ; 
    if ( SPECTROGRAPH_USEFLAG ) { SNFITSIO_SIMFLAG_SPECTROGRAPH = true ; }
  }

  OVP = ( simFlag & WRITE_MASK_SIM_MAGOBS ) ;
  if ( OVP > 0 ) // data-like, but with MAGOBS
    { SNFITSIO_SIMFLAG_MAGOBS = true ; }

  SNFITSIO_DATAFLAG = !(SNFITSIO_SIMFLAG_SNANA || SNFITSIO_SIMFLAG_MAGOBS);

  OVP = ( simFlag & WRITE_MASK_SIM_SNRMON ) ;
  if ( OVP > 0 ) { 
    SNFITSIO_SIMFLAG_SNRMON = true ; 
    sprintf(SNDATA.VARNAME_SNRMON, "SIM_SNRMAG%2.2d", 
	    SNDATA.MAGMONITOR_SNR);
  }

  OVP = ( simFlag & WRITE_MASK_COMPACT ) ; // Jan 23 2018
  if ( OVP > 0  ) { SNFITSIO_COMPACT_FLAG = true ; }

  OVP = ( simFlag & WRITE_MASK_SIM_MODELPAR ) ;
  if ( OVP > 0 ) { SNFITSIO_SIMFLAG_MODELPAR = true ; }

  IFILE_SNFITSIO = 1; // only one file written here.

  // store path and VERSION in globals 
  sprintf(SNFITSIO_DATA_PATH,    "%s", path );
  sprintf(SNFITSIO_PHOT_VERSION, "%s", version );
  
  SNFITSIO_NSUBSAMPLE_MARK = Nsubsample_mark ;

  // create all fits-filenames before opening either file
  for ( itype=0 ; itype < MXTYPE_SNFITSIO; itype++ ) {
    ptrType = snfitsType[itype] ;

    ptrFile = snfitsFile[IFILE_SNFITSIO][itype] ; // short fileName
    sprintf(ptrFile, "%s_%s.FITS", prefix, ptrType );

    // check length of file name (Aug 2019)
    lenpath=strlen(path); lenfile=strlen(ptrFile);  lentot=lenpath+lenfile;
    if ( lentot >= MXPATHLEN ) {
      print_preAbort_banner(fnam);
      printf("   path = '%s' \n", path);
      printf("   file = '%s' \n", ptrFile);
      sprintf(c1err, "filename length= %d is too long", lentot);
      sprintf(c2err, "LEN(path,file) = %d, %d : bound is MXPATHLEN=%d",
	      lenpath, lenfile, MXPATHLEN );
      errmsg(SEV_FATAL, 0, fnam, c1err, c2err ); 
    }

    ptrFile2 = snfitsFile_plusPath[IFILE_SNFITSIO][itype] ; // path/fileName
    sprintf(ptrFile2, "%s/%s", path, ptrFile );
   
  }

  // load output argument: name of header file
  sprintf(headFile, "%s", snfitsFile[IFILE_SNFITSIO][ITYPE_SNFITSIO_HEAD] );

  // misc inits

  for ( itype=0 ; itype < MXTYPE_SNFITSIO; itype++ ) {
    NPAR_SNFITSIO[itype] = 0;
    WR_SNFITSIO_TABLEVAL[itype].NROW = 0 ;
    for ( ipar=0; ipar < MXPAR_SNFITSIO ; ipar++ ) 
      { WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[ipar] = -1 ; }
  }

  wr_snfitsio_create ( ITYPE_SNFITSIO_HEAD ) ;
  wr_snfitsio_create ( ITYPE_SNFITSIO_PHOT ) ; 
  
  wr_snfitsio_init_head();
  wr_snfitsio_init_phot();

  if ( SNFITSIO_SIMFLAG_SPECTROGRAPH )  { 
    wr_snfitsio_create ( ITYPE_SNFITSIO_SPEC    ) ; 
    wr_snfitsio_create ( ITYPE_SNFITSIO_SPECTMP ) ; 
    wr_snfitsio_init_spec();
  }

} // end of  WR_SNFITSIO_INIT

void wr_snfitsio_init__(char *path, char *version, char *prefix, 
			int *simFlag, int *Nsubsample_mark, char *headFile ) {
  WR_SNFITSIO_INIT(path,version,prefix,*simFlag,*Nsubsample_mark,headFile);
}


// ========================================
void wr_snfitsio_init_head(void) {

  // Init HEADER table.
  // Dec  3, 2012: add NON1A header info (requested by S.Rodney)
  // Dec 17, 2012: add HOSTGAL mags if they exist.
  // Feb 12, 2014: add SIM_HOSTLIB_xxx params
  // Oct 05, 2015: SNID,IAUC -> "16A" instead of 12A,8A
  // Jul 16, 2016: remove SIM_AVTAU
  // May 24, 2017: add CCDNUM
  // Dec 10, 2018: add BYOSED
  // Jul 20, 2019: add strong lens info
  // Feb 27, 2020: add SIM_HOSTLIB_GALID
  // May 14, 2020: add REDSHIFT_QUALITYFLAG

  long  NROW = 0 ;
  int itype, ncol, istat, ivar, ipar ;
  int ifilt, ifilt_obs;

  fitsfile *fp;

  char parName[80] ;
  char TBLname[40] ;
  char fnam[] = "wr_snfitsio_init_head" ;

  // ------------- BEGIN --------------

  itype = ITYPE_SNFITSIO_HEAD ;
  fp    = fp_snfitsFile[itype];
  sprintf(TBLname, "%s", "Header" );
  
  // fill pointers for each header parameter
  if ( SNDATA.SUBSURVEY_FLAG )
    { wr_snfitsio_addCol("40A", "SUBSURVEY", itype); }

  wr_snfitsio_addCol( "16A", "SNID", itype   ) ;  // character
  wr_snfitsio_addCol( "16A" ,"IAUC", itype   ) ;
  wr_snfitsio_addCol( "1I" , "FAKE", itype   ) ;  // 0=data; >1 => sim

  if ( !SNFITSIO_SIMFLAG_SNANA  )
    {  wr_snfitsio_addCol( "1I" , "MASK_FLUXCOR_SNANA", itype );  }

  wr_snfitsio_addCol( "1D" , "RA"  ,    itype   ) ;  
  wr_snfitsio_addCol( "1D" , "DEC",     itype   ) ;
  wr_snfitsio_addCol( "1E" , "PIXSIZE", itype ) ;
  wr_snfitsio_addCol( "1I" , "NXPIX",   itype ) ;
  wr_snfitsio_addCol( "1I" , "NYPIX",   itype ) ;
  // xxx mark delete Mar 15 2021  wr_snfitsio_addCol("1I","CCDNUM",itype);
  wr_snfitsio_addCol( "1J" , "SNTYPE" , itype ) ; 

  wr_snfitsio_addCol( "1J" , "NOBS"       , itype ) ; 
  wr_snfitsio_addCol( "1J" , "PTROBS_MIN" , itype ) ; // pointer to light curve
  wr_snfitsio_addCol( "1J" , "PTROBS_MAX" , itype ) ; // in phot file

  wr_snfitsio_addCol( "1E", "MWEBV" ,     itype );    // Galactic extinction
  wr_snfitsio_addCol( "1E", "MWEBV_ERR" , itype );    // error on above

  wr_snfitsio_addCol( "1E", "REDSHIFT_HELIO" ,       itype ); 
  wr_snfitsio_addCol( "1E", "REDSHIFT_HELIO_ERR" ,   itype );
  wr_snfitsio_addCol( "1E", "REDSHIFT_FINAL" ,       itype );
  wr_snfitsio_addCol( "1E", "REDSHIFT_FINAL_ERR" ,   itype );

  if ( SNFITSIO_DATAFLAG ) 
    { wr_snfitsio_addCol( "1I", "REDSHIFT_QUALITYFLAG",  itype ); }

  wr_snfitsio_addCol( "1E", "VPEC" ,      itype );  // peculiar velocity cor
  wr_snfitsio_addCol( "1E", "VPEC_ERR" ,  itype );  // error on correction

  // ---------- HOST ----------
  wr_snfitsio_addCol( "1I", "HOSTGAL_NMATCH" ,     itype ); 
  wr_snfitsio_addCol( "1I", "HOSTGAL_NMATCH2" ,    itype ); 
  wr_snfitsio_addCol( "1K", "HOSTGAL_OBJID" ,      itype ); 
  wr_snfitsio_addCol( "1E", "HOSTGAL_PHOTOZ" ,     itype );
  wr_snfitsio_addCol( "1E", "HOSTGAL_PHOTOZ_ERR" , itype );
  wr_snfitsio_addCol( "1E", "HOSTGAL_SPECZ" ,      itype );
  wr_snfitsio_addCol( "1E", "HOSTGAL_SPECZ_ERR" ,  itype );
  wr_snfitsio_addCol( "1D", "HOSTGAL_RA" ,         itype );  
  wr_snfitsio_addCol( "1D", "HOSTGAL_DEC" ,        itype );  
  wr_snfitsio_addCol( "1E", "HOSTGAL_SNSEP" ,      itype );  
  wr_snfitsio_addCol( "1E", "HOSTGAL_DDLR" ,       itype );  // Jan 29 2019
  wr_snfitsio_addCol( "1E", "HOSTGAL_CONFUSION" ,  itype );  // Jan 29 2019
  wr_snfitsio_addCol( "1E", "HOSTGAL_LOGMASS" ,    itype ); 
  wr_snfitsio_addCol( "1E", "HOSTGAL_LOGMASS_ERR", itype ); 
  wr_snfitsio_addCol( "1E", "HOSTGAL_sSFR" ,       itype ); 
  wr_snfitsio_addCol( "1E", "HOSTGAL_sSFR_ERR",    itype ); 


  // add HOSTGAL mags 
  for ( ifilt=0; ifilt < SNDATA_FILTER.NDEF; ifilt++ ) {
    ifilt_obs  = SNDATA_FILTER.MAP[ifilt];
    sprintf(parName,"HOSTGAL_MAG_%c", FILTERSTRING[ifilt_obs] );
    wr_snfitsio_addCol( "1E", parName, itype );
  }
  
  // add HOSTGAL mag-errors (Feb 2019)
  for ( ifilt=0; ifilt < SNDATA_FILTER.NDEF; ifilt++ ) {
    ifilt_obs  = SNDATA_FILTER.MAP[ifilt];
    sprintf(parName,"HOSTGAL_MAGERR_%c", FILTERSTRING[ifilt_obs] );
    wr_snfitsio_addCol( "1E", parName, itype );
  }


  // Feb 7 2019: store 2nd HOSTGAL for 
  //  1) data or 2) sim with NBR_LIST in HOSTLIB

  SNFITSIO_SIMFLAG_NBR_LIST = 
    ( !SNFITSIO_SIMFLAG_SNANA || HOSTLIB.IVAR_NBR_LIST > 0 ) ;

  if ( SNFITSIO_SIMFLAG_NBR_LIST ) {
    wr_snfitsio_addCol( "1K", "HOSTGAL2_OBJID" ,      itype ); 
    wr_snfitsio_addCol( "1E", "HOSTGAL2_PHOTOZ" ,     itype );
    wr_snfitsio_addCol( "1E", "HOSTGAL2_PHOTOZ_ERR" , itype );
    wr_snfitsio_addCol( "1E", "HOSTGAL2_SPECZ" ,      itype );
    wr_snfitsio_addCol( "1E", "HOSTGAL2_SPECZ_ERR" ,  itype );
    wr_snfitsio_addCol( "1D", "HOSTGAL2_RA" ,         itype );  
    wr_snfitsio_addCol( "1D", "HOSTGAL2_DEC" ,        itype );  
    wr_snfitsio_addCol( "1E", "HOSTGAL2_SNSEP" ,      itype );  
    wr_snfitsio_addCol( "1E", "HOSTGAL2_DDLR" ,       itype ); 
    wr_snfitsio_addCol( "1E", "HOSTGAL2_LOGMASS" ,    itype ); 
    wr_snfitsio_addCol( "1E", "HOSTGAL2_LOGMASS_ERR", itype );
    wr_snfitsio_addCol( "1E", "HOSTGAL2_sSFR" ,       itype ); 
    wr_snfitsio_addCol( "1E", "HOSTGAL2_sSFR_ERR",    itype );

    // add HOSTGAL mags 
    for ( ifilt=0; ifilt < SNDATA_FILTER.NDEF; ifilt++ ) {
      ifilt_obs  = SNDATA_FILTER.MAP[ifilt];
      sprintf(parName,"HOSTGAL2_MAG_%c", FILTERSTRING[ifilt_obs] );
      wr_snfitsio_addCol( "1E", parName, itype );
    }
  
    // add HOSTGAL mag-errors (FEB 2019)
    for ( ifilt=0; ifilt < SNDATA_FILTER.NDEF; ifilt++ ) {
      ifilt_obs  = SNDATA_FILTER.MAP[ifilt];
      sprintf(parName,"HOSTGAL2_MAGERR_%c", FILTERSTRING[ifilt_obs] );
      wr_snfitsio_addCol( "1E", parName, itype );
    }
 
  }  // end of 2nd-HOSTGAL block

  // - - - -

  // HOSTGAL Surface Brightness (SB) under SN
  for ( ifilt=0; ifilt < SNDATA_FILTER.NDEF; ifilt++ ) {
    ifilt_obs  = SNDATA_FILTER.MAP[ifilt];
    sprintf(parName,"HOSTGAL_SB_FLUXCAL_%c", FILTERSTRING[ifilt_obs] );
    wr_snfitsio_addCol( "1E", parName, itype );
  }
   

  // -----------------

  wr_snfitsio_addCol( "1E", "PEAKMJD" ,        itype );
  wr_snfitsio_addCol( "1J", "SEARCH_TYPE",     itype );

  // optional PRIVATE vars.
  for ( ivar=1; ivar <= SNDATA.NVAR_PRIVATE; ivar++ ) {
    sprintf(parName,"%s", SNDATA.PRIVATE_KEYWORD[ivar] );
    wr_snfitsio_addCol( "1D", parName  , itype );
  }


  if ( SNFITSIO_SIMFLAG_SNANA ) {
    wr_snfitsio_addCol( "32A", "SIM_MODEL_NAME"     , itype );
    wr_snfitsio_addCol( "1I",  "SIM_MODEL_INDEX"    , itype );
    wr_snfitsio_addCol( "1I",  "SIM_TYPE_INDEX"     , itype );
    wr_snfitsio_addCol( "8A",  "SIM_TYPE_NAME"      , itype );

    wr_snfitsio_addCol( "1J",  "SIM_TEMPLATE_INDEX" , itype );
    wr_snfitsio_addCol( "1J",  "SIM_LIBID"          , itype );
    wr_snfitsio_addCol( "1J",  "SIM_NGEN_LIBID"     , itype );
    wr_snfitsio_addCol( "1J",  "SIM_NOBS_UNDEFINED" , itype );
    wr_snfitsio_addCol( "1J",  "SIM_SEARCHEFF_MASK" , itype );

    wr_snfitsio_addCol( "1E",  "SIM_REDSHIFT_HELIO" , itype );
    wr_snfitsio_addCol( "1E",  "SIM_REDSHIFT_CMB"   , itype );
    wr_snfitsio_addCol( "1E",  "SIM_REDSHIFT_HOST"  , itype ); 
    wr_snfitsio_addCol( "1I",  "SIM_REDSHIFT_FLAG"  , itype ); // 4.19.2019
    wr_snfitsio_addCol( "1E",  "SIM_VPEC"           , itype );
    wr_snfitsio_addCol( "1K",  "SIM_HOSTLIB_GALID"  , itype ); // Feb 2020

    for(ipar=0; ipar < SNDATA.NPAR_SIM_HOSTLIB; ipar++ ) {
      sprintf(parName,"%s", SNDATA.SIM_HOSTLIB_KEYWORD[ipar] );
      wr_snfitsio_addCol( "1E",  parName           , itype );
    }

    wr_snfitsio_addCol( "1E",  "SIM_DLMU"           , itype );
    wr_snfitsio_addCol( "1E",  "SIM_LENSDMU"        , itype );
    wr_snfitsio_addCol( "1D",  "SIM_RA"             , itype );
    wr_snfitsio_addCol( "1D",  "SIM_DEC"            , itype );
    wr_snfitsio_addCol( "1E",  "SIM_MWEBV"          , itype );
    wr_snfitsio_addCol( "1E",  "SIM_PEAKMJD"        , itype );
    wr_snfitsio_addCol( "1E",  "SIM_MAGSMEAR_COH"   , itype );      
  
    // always write SIM_AV,RV
    wr_snfitsio_addCol( "1E", "SIM_AV"          , itype );
    wr_snfitsio_addCol( "1E", "SIM_RV"          , itype );

    if ( SNDATA.SIM_MODEL_INDEX  == MODEL_SALT2 ) {
      wr_snfitsio_addCol( "1E", "SIM_SALT2x0"       , itype );
      wr_snfitsio_addCol( "1E", "SIM_SALT2x1"       , itype );
      wr_snfitsio_addCol( "1E", "SIM_SALT2c"        , itype );
      wr_snfitsio_addCol( "1E", "SIM_SALT2mB"       , itype );
      wr_snfitsio_addCol( "1E", "SIM_SALT2alpha"    , itype );
      wr_snfitsio_addCol( "1E", "SIM_SALT2beta"     , itype );
      wr_snfitsio_addCol( "1E", "SIM_SALT2gammaDM"  , itype );
    }
    if ( SNDATA.SIM_MODEL_INDEX  == MODEL_MLCS2k2 ) {
      wr_snfitsio_addCol( "1E", "SIM_DELTA"       , itype );
    }
    if ( SNDATA.SIM_MODEL_INDEX  == MODEL_SNOOPY ) {
      wr_snfitsio_addCol( "1E", "SIM_STRETCH"       , itype );
      // xxx mark delete  wr_snfitsio_addCol( "1E", "SIM_DM15", itype );
    }

    
    if ( SNDATA.SIM_MODEL_INDEX  == MODEL_NON1ASED ||
	 SNDATA.SIM_MODEL_INDEX  == MODEL_NON1AGRID ) {
      // wr_snfitsio_addCol( "1E", "SIM_AV"            , itype );
      // wr_snfitsio_addCol( "1E", "SIM_RV"            , itype );
    }

    if ( SNDATA.SIM_MODEL_INDEX == MODEL_SIMSED && 
	 SNFITSIO_SIMFLAG_MODELPAR ) {
      wr_snfitsio_addCol( "1E", "SIMSED_SALT2x0"  , itype );
      for ( ipar=0; ipar < SNDATA.NPAR_SIMSED; ipar++ ) {
	sprintf(parName,"%s", SNDATA.SIMSED_KEYWORD[ipar] );
	wr_snfitsio_addCol( "1E", parName  , itype );
      }
    }


    if ( SNDATA.SIM_MODEL_INDEX == MODEL_BYOSED ||
	 SNDATA.SIM_MODEL_INDEX == MODEL_SNEMO ) {
      for ( ipar=0; ipar < SNDATA.NPAR_PySEDMODEL; ipar++ ) {
	sprintf(parName,"%s", SNDATA.PySEDMODEL_KEYWORD[ipar] );
	wr_snfitsio_addCol( "1E", parName  , itype );
      }
    }

    if ( SNDATA.SIM_MODEL_INDEX == MODEL_LCLIB && SNFITSIO_SIMFLAG_MODELPAR) {
      for ( ipar=0; ipar < SNDATA.NPAR_LCLIB; ipar++ ) {
	sprintf(parName,"%s", SNDATA.LCLIB_KEYWORD[ipar] );
	wr_snfitsio_addCol( "1E", parName  , itype );
      }
    }


    // now do filter-dependent stuf

    // PEAKMAG
    for ( ifilt=0; ifilt < SNDATA_FILTER.NDEF; ifilt++ ) {
      ifilt_obs  = SNDATA_FILTER.MAP[ifilt];
      sprintf(parName,"SIM_PEAKMAG_%c", FILTERSTRING[ifilt_obs] );
      wr_snfitsio_addCol( "1E", parName, itype );
    }

    // TEMPLATE MAG for LCLIB model
    if ( SNDATA.SIM_MODEL_INDEX  == MODEL_LCLIB ) {
      for ( ifilt=0; ifilt < SNDATA_FILTER.NDEF; ifilt++ ) {
	ifilt_obs  = SNDATA_FILTER.MAP[ifilt];
	sprintf(parName,"SIM_TEMPLATEMAG_%c", FILTERSTRING[ifilt_obs] );
	wr_snfitsio_addCol( "1E", parName, itype );
      }      
    }

    // EXPOSURE times
    for ( ifilt=0; ifilt < SNDATA_FILTER.NDEF; ifilt++ ) {
      ifilt_obs  = SNDATA_FILTER.MAP[ifilt];
      sprintf(parName,"SIM_EXPOSURE_%c", FILTERSTRING[ifilt_obs] );
      wr_snfitsio_addCol( "1E", parName, itype );
    }

    
    // GALFRAC    
    if ( SNDATA.SIM_HOSTLIB_MSKOPT ) {
      for ( ifilt=0; ifilt < SNDATA_FILTER.NDEF; ifilt++ ) {
	ifilt_obs  = SNDATA_FILTER.MAP[ifilt];
	sprintf(parName,"SIM_GALFRAC_%c", FILTERSTRING[ifilt_obs] );
	wr_snfitsio_addCol( "1E", parName, itype );
      }
    }

    // strong lens info (Julu 2019)
    if ( SNDATA.SIM_SL_FLAG ) {
      wr_snfitsio_addCol( "1J",  "SIM_STRONGLENS_ID"       , itype );
      wr_snfitsio_addCol( "1E",  "SIM_STRONGLENS_z"        , itype );
      wr_snfitsio_addCol( "1E",  "SIM_STRONGLENS_TDELAY"   , itype );
      wr_snfitsio_addCol( "1E",  "SIM_STRONGLENS_MAGSHIFT" , itype );
      wr_snfitsio_addCol( "1I",  "SIM_STRONGLENS_NIMG"     , itype );
      wr_snfitsio_addCol( "1I",  "SIM_STRONGLENS_IMGNUM"   , itype );
    }

  } // SNFITSIO_SIMFLAG_SNANA


  // June 2017
  if ( SNFITSIO_SIMFLAG_SNANA ) {
    sprintf(parName,"%s", "SIM_SUBSAMPLE_INDEX" );
    wr_snfitsio_addCol( "1I", parName, itype );
  }

  // ----------------------
  // create header table. 
  ncol = NPAR_SNFITSIO[itype] ;  istat = 0;
  fits_create_tbl(fp, BINARY_TBL, NROW, ncol
		  ,&SNFITSIO_TABLEDEF[itype].ptrName[1]
		  ,&SNFITSIO_TABLEDEF[itype].ptrForm[1]
		  ,&SNFITSIO_TABLEDEF[itype].ptrUnit[1]
		  ,TBLname, &istat );

  sprintf(BANNER,"fits_create_tbl for %s", TBLname );
  snfitsio_errorCheck(BANNER, istat) ;



} // end of wr_snfitsio_init_head


// ==================================
void wr_snfitsio_addCol(char *tform, char *name, int itype) {

  // add table column with form *tform and *name.

  int NPAR;
  char  *ptrTmp ;
  char fnam[] = "wr_snfitsio_addCol"     ;

  // ------------- BEGIN -------------------

  // increment global parameter counter
  NPAR_SNFITSIO[itype]++ ;
  NPAR = NPAR_SNFITSIO[itype] ;


  if ( NPAR >= MXPAR_SNFITSIO ) {
    sprintf(c1err,"NPAR_SNFITSIO[%s] = %d exceeds bound", 
	    snfitsType[itype], NPAR);
    sprintf(c2err,"Current table par-name=%s  and  tform=%s", 
	    name, tform) ;
    errmsg(SEV_FATAL, 0, fnam, c1err, c2err ); 
  }

  // name of parameter
  ptrTmp = SNFITSIO_TABLEDEF[itype].name[NPAR] ;
  SNFITSIO_TABLEDEF[itype].ptrName[NPAR] = ptrTmp ;
  sprintf(ptrTmp, "%s", name ) ;

  // param  type (int, float ...)
  ptrTmp = SNFITSIO_TABLEDEF[itype].form[NPAR];  
  SNFITSIO_TABLEDEF[itype].ptrForm[NPAR] = ptrTmp ;
  sprintf(ptrTmp, "%s", tform ) ;


  // set unit to blank
  SNFITSIO_TABLEDEF[itype].ptrUnit[NPAR] = stringBlank ;


} // end of wr_snfitsio_addCol


// ========================================
void wr_snfitsio_init_phot(void) {

  // Init HEADER table.
  // Jan 2018: 
  //   + remove several obsolete columns
  //     (TELESCOPE, MAG, MAGERR )
  //     

  long  NROW = 0 ;
  int itype, ncol, istat ;
  int WRFULL = ( SNFITSIO_COMPACT_FLAG == false );
  fitsfile *fp;
  char TBLname[40] ;
  //  char fnam[] = "wr_snfitsio_init_phot" ;

  // ------------- BEGIN --------------

  itype = ITYPE_SNFITSIO_PHOT ;
  fp = fp_snfitsFile[itype];
  sprintf(TBLname, "%s", "Photometry" );

  wr_snfitsio_addCol( "1D" , "MJD"         , itype ) ;  // 1D = double
  wr_snfitsio_addCol( "2A",  "BAND"        , itype ) ; 
  // xxx mark 2/15/2021  wr_snfitsio_addCol( "2A",  "FLT"  , itype ) ; 

  wr_snfitsio_addCol( "1I",  "CCDNUM"      , itype ) ;  // Mar 2021 shortint
  wr_snfitsio_addCol( "12A", "FIELD"       , itype ) ; 

  wr_snfitsio_addCol( "1J",  "PHOTFLAG"    , itype ) ; 
  wr_snfitsio_addCol( "1E",  "PHOTPROB"    , itype ) ; 

  wr_snfitsio_addCol( "1E" , "FLUXCAL"     , itype ) ;  
  wr_snfitsio_addCol( "1E" , "FLUXCALERR"  , itype ) ;

  if ( SNDATA.NEA_PSF_UNIT ) {
    // Noise Equiv Area, pixels
    wr_snfitsio_addCol( "1E" , "PSF_NEA"   , itype ) ;  // Feb 28 2021
  }
  else {
    // traditional PSF params
    wr_snfitsio_addCol( "1E" , "PSF_SIG1"   , itype ) ; 
    wr_snfitsio_addCol( "1E" , "PSF_SIG2"   , itype ) ; 
    wr_snfitsio_addCol( "1E" , "PSF_RATIO"  , itype ) ;   
  }

  wr_snfitsio_addCol( "1E" , "SKY_SIG"    , itype ) ; 

  if( WRFULL ) {
    wr_snfitsio_addCol( "1E" , "SKY_SIG_T"  , itype ) ; // OPTIONAL
    wr_snfitsio_addCol( "1E" , "RDNOISE"    , itype ) ; // OPTIONAL e- per pix
  }
  wr_snfitsio_addCol( "1E" , "ZEROPT"     , itype ) ; // REQUIRED
  if( WRFULL ) {
    wr_snfitsio_addCol( "1E" , "ZEROPT_ERR" , itype ) ; // OPTIONAL
    wr_snfitsio_addCol( "1E" , "GAIN"       , itype ) ; // OPTIONAL
  }

  if ( SNDATA.NXPIX > 0 ) {
    wr_snfitsio_addCol( "1E" , "XPIX" , itype ) ;
    wr_snfitsio_addCol( "1E" , "YPIX" , itype ) ;
  }

  if ( SNFITSIO_SIMFLAG_SNANA || SNFITSIO_SIMFLAG_MAGOBS ) {
    wr_snfitsio_addCol( "1E" , "SIM_MAGOBS"  , itype ) ;
  }

  if ( SNFITSIO_SIMFLAG_SNANA && WRFULL ) {
    wr_snfitsio_addCol( "1E" , "SIM_FLUXCAL_HOSTERR" , itype ) ;
  }

  if ( SNFITSIO_SIMFLAG_SNRMON ) {
    wr_snfitsio_addCol( "1E" , SNDATA.VARNAME_SNRMON, itype ) ; 
  }

  // create header table. 
  ncol = NPAR_SNFITSIO[itype] ;  istat = 0;
  fits_create_tbl(fp, BINARY_TBL, NROW, ncol
		  ,&SNFITSIO_TABLEDEF[itype].ptrName[1]
		  ,&SNFITSIO_TABLEDEF[itype].ptrForm[1]
		  ,&SNFITSIO_TABLEDEF[itype].ptrUnit[1]
		  ,TBLname, &istat ) ;

  sprintf(BANNER,"fits_create_tbl for %s", TBLname );
  snfitsio_errorCheck(BANNER, istat) ;


  return ;

} // end of   wr_snfitsio_init_phot

// ========================================
void wr_snfitsio_init_spec(void) {

  // Created Aug 2016
  // Init tables for simulated spectra (SPECTROGRAPH feature)
  //
  // Table 1 has  3 columns which are filled here
  //                 LAMINDEX LAMMIN LAMMAX
  //         --> can use I*2 index in 2nd table.
  //
  // Table 2 is a one-row summary per spec.
  //
  // Table 3 is to store spectra:  ILAM FLUX FLUXERR SIM_MAG*100
  //
  // Mar 24 2019: include optional WARP*1000
  // Mar 25 2019: add SNR_COMPUTE and LAMOBS_SNR to table.

  long  NROW = 0 ;
  int itype, ncol, istat, ipar ;
  int FORMAT_LAMCEN  = ( INPUTS_SPECTRO.FORMAT_MASK & 1 );
  fitsfile *fp;
  char TBLname[40] ;
  //  char fnam[] = "wr_snfitsio_init_spec" ;

  // --------------- BEGIN ---------------

  itype = ITYPE_SNFITSIO_SPEC ;
  fp = fp_snfitsFile[itype];

  // ---------------------------------------------------
  // ------------------- Table 1 -----------------------
  // ---------------------------------------------------
  // first create & write table 1 mapping LAMINDEX to wavelength range.
  sprintf(TBLname, "%s", "SPECTRO_LAMINDEX" );
  wr_snfitsio_addCol( "1J",  "LAMINDEX" , itype ) ; 

  if ( FORMAT_LAMCEN ) {
    wr_snfitsio_addCol( "1E" , "LAMCEN"   , itype ) ;  
  }
  else {
    wr_snfitsio_addCol( "1E" , "LAMMIN"   , itype ) ;  
    wr_snfitsio_addCol( "1E" , "LAMMAX"   , itype ) ;  
  }

  // create header table. 
  ncol = NPAR_SNFITSIO[itype] ;  istat = 0;
  fits_create_tbl(fp, BINARY_TBL, NROW, ncol
		  ,&SNFITSIO_TABLEDEF[itype].ptrName[1]
		  ,&SNFITSIO_TABLEDEF[itype].ptrForm[1]
		  ,&SNFITSIO_TABLEDEF[itype].ptrUnit[1]
		  ,TBLname, &istat ) ;

  sprintf(BANNER,"fits_create_tbl for %s", TBLname );
  snfitsio_errorCheck(BANNER, istat) ;


  // file LAMBDA-map table right here
  int ilam, LOC, *ptrColnum ;
  int NBIN_LAM = INPUTS_SPECTRO.NBIN_LAM ;

  for( ilam=0; ilam < NBIN_LAM ; ilam++ ) {
    WR_SNFITSIO_TABLEVAL[itype].NROW++ ;
    LOC = 0;

    // ilam
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1J = ilam ;
    wr_snfitsio_fillTable ( ptrColnum, "LAMINDEX", itype );
    
    // min & max lambda in this wavelength bin
    if ( FORMAT_LAMCEN  ) {
      LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
      WR_SNFITSIO_TABLEVAL[itype].value_1E = INPUTS_SPECTRO.LAMAVG_LIST[ilam] ;
      wr_snfitsio_fillTable ( ptrColnum, "LAMCEN", itype );
    }
    else {
      LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
      WR_SNFITSIO_TABLEVAL[itype].value_1E = INPUTS_SPECTRO.LAMMIN_LIST[ilam] ;
      wr_snfitsio_fillTable ( ptrColnum, "LAMMIN", itype );
    
      LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
      WR_SNFITSIO_TABLEVAL[itype].value_1E = INPUTS_SPECTRO.LAMMAX_LIST[ilam] ;
      wr_snfitsio_fillTable ( ptrColnum, "LAMMAX", itype ); 
    }

  }


  // ---------------------------------------------------
  // ------------------- Table 2 -----------------------
  // ---------------------------------------------------
  // delete Table 1 LAMBDA-MAP, and create spec-summary table.
  // --> One row summary per spectrum.
  NPAR_SNFITSIO[itype] = 0;
  WR_SNFITSIO_TABLEVAL[itype].NROW = 0 ;
  for ( ipar=0; ipar < MXPAR_SNFITSIO ; ipar++ ) 
    { WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[ipar] = -1 ; }
  
  // -----------------
  // create table for spectra fluxes 
  sprintf(TBLname, "%s", "SPECTRO_HEADER" );
  wr_snfitsio_addCol( "16A", "SNID",        itype   ) ; 
  wr_snfitsio_addCol( "1D",  "MJD",         itype   ) ;  
  wr_snfitsio_addCol( "1E",  "Texpose",     itype   ) ; 
  wr_snfitsio_addCol( "1E",  "SNR_COMPUTE", itype   ) ; 
  wr_snfitsio_addCol( "1E",  "LAMMIN_SNR",  itype   ) ; 
  wr_snfitsio_addCol( "1E",  "LAMMAX_SNR",  itype   ) ; 

  wr_snfitsio_addCol( "1I",  "NBIN_LAM",    itype   ) ; 
  wr_snfitsio_addCol( "1J",  "PTRSPEC_MIN", itype   ) ; 
  wr_snfitsio_addCol( "1J",  "PTRSPEC_MAX", itype   ) ; 

  ncol = NPAR_SNFITSIO[itype] ;  istat = 0;
  fits_create_tbl(fp, BINARY_TBL, NROW, ncol
		  ,&SNFITSIO_TABLEDEF[itype].ptrName[1]
		  ,&SNFITSIO_TABLEDEF[itype].ptrForm[1]
		  ,&SNFITSIO_TABLEDEF[itype].ptrUnit[1]
		  ,TBLname, &istat ) ;

  sprintf(BANNER,"fits_create_tbl for %s", TBLname );
  snfitsio_errorCheck(BANNER, istat) ;


  // ---------------------------------------------------
  // ------------------- Table 3 -----------------------
  // ---------------------------------------------------

  // flux table; this could be HUUUUUGE.
  itype = ITYPE_SNFITSIO_SPECTMP ;
  fp    = fp_snfitsFile[itype];

  sprintf(TBLname, "%s", "SPECTRO_FLUX" );
  wr_snfitsio_addCol( "1I", "LAMINDEX",    itype   ) ; 
  wr_snfitsio_addCol( "1E", "FLAM",        itype   ) ;  
  wr_snfitsio_addCol( "1E", "FLAMERR",     itype   ) ;  

  if ( SNFITSIO_SIMFLAG_SNANA ) {
    wr_snfitsio_addCol( "1E", "SIM_FLAM",    itype   ) ;  // Feb 2021
    //    wr_snfitsio_addCol( "1I", "SIM_MAG",     itype   ) ; 
  }
  if ( GENSPEC.USE_WARP ) 
    { wr_snfitsio_addCol( "1I", "SIM_WARP",  itype   ) ; }
  
  ncol = NPAR_SNFITSIO[itype] ;  istat = 0;
  fits_create_tbl(fp, BINARY_TBL, NROW, ncol
		  ,&SNFITSIO_TABLEDEF[itype].ptrName[1]
		  ,&SNFITSIO_TABLEDEF[itype].ptrForm[1]
		  ,&SNFITSIO_TABLEDEF[itype].ptrUnit[1]
		  ,TBLname, &istat ) ;

  sprintf(BANNER,"fits_create_tbl for %s", TBLname );
  snfitsio_errorCheck(BANNER, istat) ;

  return ;

} // end wr_snfitsio_init_spec


// ==================================
void wr_snfitsio_create(int itype ) {

  // create fits file of 'itype' (HEAD or PHOT or SPEC)
  // and write global header info.
  //
  // Nov 24, 2012: add optional PRIVATE variable names
  // Sep 19, 2013: write SIMOPT_MWCOLORLAW, SIMOPT_MWEBV
  // Dec 07, 2013: write PRIVATE keys before checking SIMFLAG
  //
  // Feb 12, 2014: 
  //  - write SNFITSIO_CODE_IVERSION
  //  - write optional name of HOSTLIB_FILE
  //  - write optional SIM_HOSTLIB key-names in header
  //
  // Dec 27 2015: SIMLIB -> SIMLIB_FILE and add SIMLIB_MSKOPT
  //
  // Aug 02 2016: new 'SPEC' option to write simulated spectra
  // Jun 16 2017: write NSUBSAMPLE_MARK for simulation
  // Dec 26 2018: increment SNFITSIO_CODE_IVERSION for SIMSED ipar

  int istat, ipar, ivar, NVAR ;
  long NAXIS = 1, NAXES = 0    ;

  fitsfile  *fp ;

  char *ptrFile, *ptrType ;
  char KEYNAME[60], PARNAME[80] ;
  char fnam[] = "wr_snfitsio_create" ;
    
  // -------------- BEGIN --------------

  ptrFile = snfitsFile_plusPath[IFILE_SNFITSIO][itype] ;
  ptrType = snfitsType[itype] ;
	 
  // create file
  istat = 0;
  fits_create_file(&fp_snfitsFile[itype], ptrFile, &istat) ;
  sprintf(c1err,"fits_create_file for %s (%s)", ptrType, fnam);
  snfitsio_errorCheck(c1err, istat) ;

  fp = fp_snfitsFile[itype];

  // create mandatory primary image (length=0)
  fits_create_img(fp, FLOAT_IMG, NAXIS, &NAXES, &istat) ;
  sprintf(c1err,"Create zero-len primary %s-image", ptrType) ;
  snfitsio_errorCheck(c1err, istat) ;

  // --- add global header keys

  // CODE_VERSION (Feb 2014)
  //  SNFITSIO_CODE_IVERSION = 2; // Feb 11 2014 - first usage 
  //  SNFITSIO_CODE_IVERSION = 3; // Aug 7 2014 - add NXPIX,NYPIX,XPIX,YPIX
  //  SNFITSIO_CODE_IVERSION = 4; // Sep 3, 2014: add HOSTGAL_MAG & SB
  //  SNFITSIO_CODE_IVERSION = 5; // Aug 2 2016: add sim spectra
  //  SNFITSIO_CODE_IVERSION = 6; // May 24 2017: add CCDNUM to header
  //  SNFITSIO_CODE_IVERSION = 7; // Mar 18 2018: add SNRMAG[mag]
  //  SNFITSIO_CODE_IVERSION = 8; // Dec 26 2018: SIMSED_PAR loops 0 to NPAR-1
  //  SNFITSIO_CODE_IVERSION = 9; // FEB 8 2019: more HOSTGAL stuff
  SNFITSIO_CODE_IVERSION = 10 ; // Sep 10 2020: PySEDMODEL
 
  fits_update_key(fp, TINT, "CODE_IVERSION", &SNFITSIO_CODE_IVERSION, 
		  "Internal SNFTSIO code version", &istat );

  // SNANA_PATH  
  fits_update_key(fp, TSTRING, "SNANA_PATH",
                  PATH_SNANA_DIR, "SNANA code directory", &istat );

  // SNANA_VERSION
  fits_update_key(fp, TSTRING, "SNANA_VERSION",
                  SNANA_VERSION_CURRENT, "SNANA version", &istat );

  // name of survey
  fits_update_key(fp, TSTRING, "SURVEY",
                  SNDATA.SURVEY_NAME, "Survey", &istat );

  // check for sub-survey (only for data)
  SNDATA.SUBSURVEY_FLAG = 0;
  if ( strcmp(SNDATA.SURVEY_NAME,SNDATA.SUBSURVEY_NAME) != 0 )
    { SNDATA.SUBSURVEY_FLAG = 1;}
  fits_update_key(fp, TINT, "SUBSURVEY_FLAG",
                  &SNDATA.SUBSURVEY_FLAG, "SUBSURVEY_FLAG", &istat );

  // July 21 2018
  fits_update_key(fp, TINT, "MWEBV_APPLYFLAG",
		  &SNDATA.APPLYFLAG_MWEBV,
		  "1 -> Apply MWEBV cor to FLUXCAL", &istat );
  
  // List of filters
  fits_update_key(fp, TSTRING, "FILTERS",
                  SNDATA_FILTER.LIST, "List of Filters", &istat );

  // photometry version
  fits_update_key(fp, TSTRING, "VERSION",
                  SNFITSIO_PHOT_VERSION, "Photometry Version", &istat );


  // photometry fits filename
  fits_update_key(fp, TSTRING, "PHOTFILE",
                  snfitsFile[IFILE_SNFITSIO][ITYPE_SNFITSIO_PHOT],
		  "Photometry FITS file", &istat );

  // optional: name of spectrograph file
  if( SNFITSIO_SIMFLAG_SPECTROGRAPH ) {
    fits_update_key(fp, TSTRING, "SPECFILE",
		    snfitsFile[IFILE_SNFITSIO][ITYPE_SNFITSIO_SPEC],
		    "SPECTROGRAPH FITS file", &istat );
  }

  // ----------------------------------
  // check for private header variables
  if ( itype == ITYPE_SNFITSIO_HEAD ) {
    NVAR = SNDATA.NVAR_PRIVATE; 

    sprintf(KEYNAME,"NPRIVATE");
    fits_update_key(fp, TINT, KEYNAME, &NVAR,
		      "Number of private variables", &istat );

    for ( ivar=1; ivar <= NVAR; ivar++ ) {
      sprintf(PARNAME,"%s", SNDATA.PRIVATE_KEYWORD[ivar] );
      sprintf(KEYNAME, "PRIVATE%d", ivar);
      fits_update_key(fp, TSTRING, KEYNAME, PARNAME,
		      "name of private variable", &istat );
    }
  }

  // ----------------------------------
  // sim Flag
  char datatype[20], comment[80];

  if ( SNFITSIO_SIMFLAG_SNANA ) { 
    sprintf(datatype, "SIM_SNANA" ); 
    sprintf(comment,  "SNANA Simulation");
  }
  else if ( SNFITSIO_SIMFLAG_MAGOBS ) { 
    sprintf(datatype, "SIM_MAGOBS" );
    sprintf(comment,  "data-like with SIM_MAGOBS");
  }
  else  { 
    sprintf(datatype, "DATA" ); 
    sprintf(comment,  "real data");
  }

  fits_update_key(fp, TSTRING, "DATATYPE", datatype, comment, &istat ); 


  // --------------------------------------------
  // simulation info (optional)
  if ( !SNFITSIO_SIMFLAG_SNANA ) { return ; }

  // @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
  // @@@@@@@ if we are here, write SIM_XXX info @@@@@@@@@@@
  // @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

  // simlib file
  istat=0;
  fits_update_key(fp, TSTRING, "SIMLIB_FILE", SNDATA.SIMLIB_FILE, 
		  "SIMLIB Cadence/conditions File", &istat );  
  sprintf(c1err,"Write SIMLIB file name") ;
  snfitsio_errorCheck(c1err, istat) ;

  istat=0;
  fits_update_key(fp, TINT, "SIMLIB_MSKOPT", &SNDATA.SIMLIB_MSKOPT, 
		  "SIMLIB options mask", &istat );  
  sprintf(c1err,"Write SIMLIB MSKOPT") ;
  snfitsio_errorCheck(c1err, istat) ;


  // hostlib file (optional)
  if ( strlen(SNDATA.HOSTLIB_FILE) > 0 ) {
    istat=0;
    fits_update_key(fp, TSTRING, "HOSTLIB_FILE", SNDATA.HOSTLIB_FILE, 
		    "name of HOSTLIB file", &istat );  
    sprintf(c1err,"Write HOSTLIB file name") ;
    snfitsio_errorCheck(c1err, istat) ;   
  }

  // Sep 2013 - write MWEBV options for color law and MWEBV-modification
  istat = 0 ;
  fits_update_key(fp, TINT, "SIMOPT_MWCOLORLAW", &SNDATA.SIMOPT_MWCOLORLAW, 
		  "option for MW color law", &istat );
  sprintf(c1err,"Write SIMOPT_MWCOLORLAW") ;
  snfitsio_errorCheck(c1err, istat) ;


  istat = 0;
  fits_update_key(fp, TFLOAT, "SIM_MWRV", &SNDATA.SIM_MWRV, 
		  "RV for Galactic extinction", &istat );
  sprintf(c1err,"Write SIMOPT_MWCOLORLAW") ;
  snfitsio_errorCheck(c1err, istat) ;

  istat = 0 ;
  fits_update_key(fp, TINT, "SIMOPT_MWEBV", &SNDATA.SIMOPT_MWEBV, 
		  "option for MWEBV_SFD", &istat );
  sprintf(c1err,"Write SIMOPT_MWEBV") ;
  snfitsio_errorCheck(c1err, istat) ;

  // write option to fudge flux errors (Oct 30 2015)
  istat = 0 ;
  fits_update_key(fp, TINT, "SIMOPT_FLUXERR", &SNDATA.SIMOPT_FLUXERR, 
		  "option for fudgeing fluxErrors", &istat );
  sprintf(c1err,"Write SIMOPT_FLUXERR") ;
  snfitsio_errorCheck(c1err, istat) ;

  if ( SNFITSIO_SIMFLAG_SPECTROGRAPH ) {
    istat=0;
    fits_update_key(fp, TSTRING, "SPECTROGRAPH_INSTRUMENT", 
		    INPUTS_SPECTRO.INSTRUMENT_NAME,
		    "name of SPECTROGRAPH instrument", &istat );  
    sprintf(c1err,"Write SPECTROGRAPH_INSTRUMENT name") ;
    snfitsio_errorCheck(c1err, istat) ; 
  }

  // for SIMSED model, must write out the parameter names here
  // so that the unpacking code knows what to look for.
  // Other models can be mixed (i.e., Ia + nonIa) so the model
  // info appears in the HEAD table for each SN.

  int NPAR;

  NPAR = SNDATA.NPAR_SIMSED ;  
  if ( NPAR > 0 && SNFITSIO_SIMFLAG_MODELPAR ) {
    fits_update_key(fp, TSTRING, "SIMSED_MODEL",
		    SNDATA.SIM_MODEL_NAME, "Generation Model", &istat );

    fits_update_key(fp, TINT, "SIMSED_NPAR", &NPAR,
		    "Number of SIMSED params", &istat );

    for ( ipar=0; ipar < NPAR; ipar++ ) {
      sprintf(KEYNAME,"SIMSED_PAR%2.2d", ipar ); //write fortran-like index 
      sprintf(PARNAME,"%s", SNDATA.SIMSED_KEYWORD[ipar] );
      fits_update_key(fp, TSTRING, KEYNAME, PARNAME,
		      "SIMSED column name", &istat );  
    } // ipar
  }  // SIMSED 


  // idem for BYOSED,SNMEO params (Dec 10 2018)
  NPAR = SNDATA.NPAR_PySEDMODEL ;  
  if ( NPAR > 0 ) {
    // xxxx    sprintf(KEYNAME, "%s_MODEL", SNDATA.SIM_MODEL_NAME);
    sprintf(KEYNAME, "PySEDMODEL" );
    fits_update_key(fp, TSTRING, KEYNAME, SNDATA.SIM_MODEL_NAME, 
		    "Generation Model", &istat );

    sprintf(KEYNAME, "%s_NPAR", SNDATA.SIM_MODEL_NAME);
    fits_update_key(fp, TINT, KEYNAME, &NPAR, 
		    "Number of PySEDMODEL params", &istat ); 

    for ( ipar=0; ipar < NPAR; ipar++ ) {
      sprintf(KEYNAME,"%s_PAR%2.2d", SNDATA.SIM_MODEL_NAME, ipar );
      sprintf(PARNAME,"%s", SNDATA.PySEDMODEL_KEYWORD[ipar] );
      fits_update_key(fp, TSTRING, KEYNAME, PARNAME,
		      "PySEDMODEL column name", &istat );  
    } // ipar
  }  // PySEDMODEL



  // idem for LCLIB params (Sep 8 2017)
  NPAR = SNDATA.NPAR_LCLIB ;  
  if ( NPAR > 0 && SNFITSIO_SIMFLAG_MODELPAR ) {
    fits_update_key(fp, TSTRING, "LCLIB_MODEL",
		    SNDATA.SIM_MODEL_NAME, "Generation Model", &istat );

    fits_update_key(fp, TINT, "LCLIB_NPAR", &NPAR,
		    "Number of LCLIB params", &istat );

    for ( ipar=0; ipar < NPAR; ipar++ ) {
      sprintf(KEYNAME,"LCLIB_PAR%2.2d", ipar );
      sprintf(PARNAME,"%s", SNDATA.LCLIB_KEYWORD[ipar] );
      fits_update_key(fp, TSTRING, KEYNAME, PARNAME,
		      "LCLIB column name", &istat );  
    } // ipar
  }  // LCLIB


  // Write SIM_HOSTLIB_XXX parameters in the same way as for SIMSED
  NPAR = SNDATA.NPAR_SIM_HOSTLIB ;
  if ( NPAR > 0 ) {
    fits_update_key(fp, TINT, "SIM_HOSTLIB_NPAR", &NPAR,
		    "Number of SIM_HOSTLIB params", &istat );

    for ( ipar=0; ipar < NPAR; ipar++ ) {
      sprintf(KEYNAME,"SIM_HOSTLIB_PAR%2.2d", ipar );
      sprintf(PARNAME,"%s", SNDATA.SIM_HOSTLIB_KEYWORD[ipar] );
      fits_update_key(fp, TSTRING, KEYNAME, PARNAME,
		      "HOSTLIB column name", &istat );  
    } // ipar
  }  //NPAR


  // Jun 2017: write SNFITSIO_NSUBSAMPLE_MARK
  fits_update_key(fp, TINT, "SIM_NSUBSAMPLE_MARK", 
		  &SNFITSIO_NSUBSAMPLE_MARK,
		  "Number of marked subSamples", &istat );

  // Mar 18 2018: write VARNAME_SNRMON
  fits_update_key(fp, TSTRING, "SIM_VARNAME_SNRMON", SNDATA.VARNAME_SNRMON,
		  "PHOT varName for SNR(MAGMONITOR)", &istat );  

  // Feb 10 2021: write SIM_SL_FLAG for strong lensing
  fits_update_key(fp, TINT, "SIM_SL_FLAG", &SNDATA.SIM_SL_FLAG,
		  "Strong lens flag", &istat );  

  return ;

} // end of wr_snfitsio_create



// ==================================
void WR_SNFITSIO_UPDATE(void) {

  // Update HEADER and PHOT light curve in fits files.

  int  ep, NUSE_EPOCH;
  char fnam[] = "WR_SNFITSIO_UPDATE" ;

  // --------------- BEGIN --------------


  // ----------- START WITH HEADER --------------
  wr_snfitsio_update_head() ;

  // Now the photometry.  Note that NEPOCH includes peak values
  // (for sim) and epochs with undefined model-mags.
  // NOBS <= NPEOCH is the number of valid observations.
  // The logical array SNDATA.USE_EPOCH[ep] picks out the
  // valid observations from the EPOCH list.
  // Finally, NEPOCH+1 is an end-of-lightcurve buffer
  // that is created iternally; it is used in the readback
  // to ensure that the epoch-pointers are OK.

  // start by filling end-of-event marker
  ep                        = SNDATA.NEPOCH+1; // artificial epoch for EOE
  SNDATA.OBSFLAG_WRITE[ep]  = true ;
  SNDATA.MJD[ep]            = SNFITSIO_EOE_MARKER ;
  SNDATA.FLUXCAL[ep]        = SNFITSIO_EOE_MARKER ;
  SNDATA.FLUXCAL_ERRTOT[ep] = SNFITSIO_EOE_MARKER ;
  sprintf(SNDATA.FILTCHAR[ep],  "%s", "-");
  sprintf(SNDATA.FIELDNAME[ep], "%s", "XXXX" ) ;
  sprintf(SNDATA.TELESCOPE[ep], "%s", "XXXX" ) ;

  // loop over epochs and fill fits table.
  NUSE_EPOCH = 0;
  for ( ep = 1; ep <= SNDATA.NEPOCH+1; ep++ ) {

    if ( !SNDATA.OBSFLAG_WRITE[ep] )  { continue ; }

    wr_snfitsio_update_phot(ep) ;

    if ( ep <= SNDATA.NEPOCH ) { NUSE_EPOCH++ ; }

  }

  // make sure that NUSE_EPOCH matches the expected 
  // Number of observations (NOBS)
  if ( NUSE_EPOCH != SNDATA.NOBS ) {
    sprintf(c1err,"NUSE_EPOCH=%d != NOBS=%d", NUSE_EPOCH, SNDATA.NOBS );
    sprintf(c2err,"SNDATA.NEPOCH=%d", SNDATA.NEPOCH);
    errmsg(SEV_FATAL, 0, fnam, c1err, c2err ); 
  }


  // Aug 2016: check for optional spectra
  // Beware that NMJD_TOT is total number of requested spectra,
  // while NMJD_PROC is how many spectra exist
  // (e.g., Trest outside sim-model range can't create spectra)
  int imjd;
  if ( SNFITSIO_SIMFLAG_SPECTROGRAPH ) {
    for(imjd=0; imjd < GENSPEC.NMJD_TOT; imjd++ ) 
      { wr_snfitsio_update_spec(imjd) ; }
  }

  return ;

} // end of WR_SNFITSIO_UPDATE

void wr_snfitsio_update__(void) {
  WR_SNFITSIO_UPDATE();
}


// ==================================
void wr_snfitsio_update_head(void) {

  // May 20 2020: fix bug setting parName for SIM_STRONGLENS_XXX

  int itype, LOC ,*ptrColnum, ipar, ivar    ;
  int  PTROBS_MIN, PTROBS_MAX;
  int  ifilt, ifilt_obs ;
  char parName[80];
  char fnam[] = "wr_snfitsio_update_head" ;
  
  // ------------- BEGIN -------------

  // init local HEADER-pointer for colnum lookup
  LOC = 0;
  itype = ITYPE_SNFITSIO_HEAD ;
  WR_SNFITSIO_TABLEVAL[itype].NROW++ ;

  // calculate obs-pointer for photometry fits file.
  PTROBS_MIN = WR_SNFITSIO_TABLEVAL[ITYPE_SNFITSIO_PHOT].NROW+1 ;
  PTROBS_MAX = PTROBS_MIN - 1 + SNDATA.NOBS;

  if ( SNDATA.SUBSURVEY_FLAG ) {
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_A = SNDATA.SUBSURVEY_NAME ;
    wr_snfitsio_fillTable ( ptrColnum, "SUBSURVEY", itype );
  }

  // SNID 
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_A = SNDATA.CCID ;
  wr_snfitsio_fillTable ( ptrColnum, "SNID", itype );

  // IAUC name
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_A = SNDATA.IAUC_NAME ;
  wr_snfitsio_fillTable ( ptrColnum, "IAUC", itype );

  // fake flag
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1I = (short int)SNDATA.FAKE ;
  wr_snfitsio_fillTable ( ptrColnum, "FAKE", itype );

  // mask of fluxcor fudges (Nov 2018); real data only
  if ( !SNFITSIO_SIMFLAG_SNANA  ) {
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1I = (short int)SNDATA.MASK_FLUXCOR ;
    wr_snfitsio_fillTable ( ptrColnum, "MASK_FLUXCOR_SNANA", itype );
  }

  // RA & DEC
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1D = SNDATA.RA ;
  wr_snfitsio_fillTable ( ptrColnum, "RA", itype );

  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1D = SNDATA.DEC ;
  wr_snfitsio_fillTable ( ptrColnum, "DEC", itype );

  // PIXEL size
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.PIXSIZE ;
  wr_snfitsio_fillTable ( ptrColnum, "PIXSIZE", itype );

  // NXPIX (Aug 7 2014)
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1I = SNDATA.NXPIX ;
  wr_snfitsio_fillTable ( ptrColnum, "NXPIX", itype );

  // NYPIX (Aug 7 2014)
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1I = SNDATA.NYPIX ;
  wr_snfitsio_fillTable ( ptrColnum, "NYPIX", itype );

  /* xxx mark delete Mar 15 2021 ... now it's only in PHOT table
  // CCDNUM (May 24 2017)
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1I = SNDATA.CCDNUM[0] ;
  wr_snfitsio_fillTable ( ptrColnum, "CCDNUM", itype );
  xxxxxxx */

  // SNTYPE
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1J = SNDATA.SNTYPE ;
  wr_snfitsio_fillTable ( ptrColnum, "SNTYPE", itype );

  // NOBS
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1J = SNDATA.NOBS ;
  wr_snfitsio_fillTable ( ptrColnum, "NOBS", itype );

  // PTROBS_MIN/MAX
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1J = PTROBS_MIN ;
  wr_snfitsio_fillTable ( ptrColnum, "PTROBS_MIN", itype );

  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1J = PTROBS_MAX ;
  wr_snfitsio_fillTable ( ptrColnum, "PTROBS_MAX", itype );


  // MWEBV
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.MWEBV ;
  wr_snfitsio_fillTable ( ptrColnum, "MWEBV", itype );

  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.MWEBV_ERR ;
  wr_snfitsio_fillTable ( ptrColnum, "MWEBV_ERR", itype );


  // Z_HELIO and error
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.REDSHIFT_HELIO ;
  wr_snfitsio_fillTable ( ptrColnum, "REDSHIFT_HELIO", itype );

  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.REDSHIFT_HELIO_ERR ;
  wr_snfitsio_fillTable ( ptrColnum, "REDSHIFT_HELIO_ERR", itype );


  // Z_FINAL = ZCMB, and error
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.REDSHIFT_FINAL ;
  wr_snfitsio_fillTable ( ptrColnum, "REDSHIFT_FINAL", itype );

  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.REDSHIFT_FINAL_ERR ;
  wr_snfitsio_fillTable ( ptrColnum, "REDSHIFT_FINAL_ERR", itype );

  if ( SNFITSIO_DATAFLAG ) {
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1I = SNDATA.REDSHIFT_QUALITYFLAG ;
    wr_snfitsio_fillTable ( ptrColnum, "REDSHIFT_QUALITYFLAG", itype );
  }

  // VPEC and error (Jan 2018)
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.VPEC ;
  wr_snfitsio_fillTable ( ptrColnum, "VPEC", itype );

  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.VPEC_ERR ;
  wr_snfitsio_fillTable ( ptrColnum, "VPEC_ERR", itype );

  // ---------- HOST --------------
  int igal, NHOSTGAL=1;  char PREFIX[20]="HOSTGAL" ;
  if ( SNFITSIO_SIMFLAG_NBR_LIST ) { NHOSTGAL = MXHOSTGAL; }

  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  sprintf(parName,"%s_NMATCH", PREFIX);
  WR_SNFITSIO_TABLEVAL[itype].value_1I = SNDATA.HOSTGAL_NMATCH[0] ;
  wr_snfitsio_fillTable ( ptrColnum, parName, itype );  

  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  sprintf(parName,"%s_NMATCH2", PREFIX);
  WR_SNFITSIO_TABLEVAL[itype].value_1I = SNDATA.HOSTGAL_NMATCH[1] ;
  wr_snfitsio_fillTable ( ptrColnum, parName, itype );  

  for(igal=0; igal < NHOSTGAL; igal++ ) {

    if ( igal > 0 ) { sprintf(PREFIX,"HOSTGAL%d", igal+1); } 

    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    sprintf(parName,"%s_OBJID", PREFIX);
    WR_SNFITSIO_TABLEVAL[itype].value_1K = SNDATA.HOSTGAL_OBJID[igal] ;
    wr_snfitsio_fillTable ( ptrColnum, parName, itype );  

    // HOST photoz and its error
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    sprintf(parName,"%s_PHOTOZ", PREFIX);
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.HOSTGAL_PHOTOZ[igal] ;
    wr_snfitsio_fillTable ( ptrColnum, parName, itype );
    
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    sprintf(parName,"%s_PHOTOZ_ERR", PREFIX);
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.HOSTGAL_PHOTOZ_ERR[igal] ;
    wr_snfitsio_fillTable ( ptrColnum, parName, itype );

    // HOST specZ and its error
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    sprintf(parName,"%s_SPECZ", PREFIX);
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.HOSTGAL_SPECZ[igal] ;
    wr_snfitsio_fillTable ( ptrColnum, parName, itype );
    
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    sprintf(parName,"%s_SPECZ_ERR", PREFIX);
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.HOSTGAL_SPECZ_ERR[igal] ;
    wr_snfitsio_fillTable ( ptrColnum, parName, itype );

    // host coords (Added Feb 18 2019)
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    sprintf(parName,"%s_RA", PREFIX);
    WR_SNFITSIO_TABLEVAL[itype].value_1D = SNDATA.HOSTGAL_RA[igal] ;
    wr_snfitsio_fillTable ( ptrColnum, parName, itype );

    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    sprintf(parName,"%s_DEC", PREFIX);
    WR_SNFITSIO_TABLEVAL[itype].value_1D = SNDATA.HOSTGAL_DEC[igal] ;
    wr_snfitsio_fillTable ( ptrColnum, parName, itype );
    
    // host-SN separation
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    sprintf(parName,"%s_SNSEP", PREFIX);
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.HOSTGAL_SNSEP[igal] ;
    wr_snfitsio_fillTable ( ptrColnum, parName, itype );
    
    // direction light-radius
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    sprintf(parName,"%s_DDLR", PREFIX);
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.HOSTGAL_DDLR[igal] ;
    wr_snfitsio_fillTable ( ptrColnum, parName, itype );
   
    // host mass
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    sprintf(parName,"%s_LOGMASS", PREFIX);
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.HOSTGAL_LOGMASS_OBS[igal] ;
    wr_snfitsio_fillTable ( ptrColnum, parName, itype );
    
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    sprintf(parName,"%s_LOGMASS_ERR", PREFIX);
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.HOSTGAL_LOGMASS_ERR[igal] ;
    wr_snfitsio_fillTable ( ptrColnum, parName, itype );

    // host sSFR
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    sprintf(parName,"%s_sSFR", PREFIX);
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.HOSTGAL_sSFR[igal] ;
    wr_snfitsio_fillTable ( ptrColnum, parName, itype );
    
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    sprintf(parName,"%s_sSFR_ERR", PREFIX);
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.HOSTGAL_sSFR_ERR[igal] ;
    wr_snfitsio_fillTable ( ptrColnum, parName, itype );


    // HOSTGAL MAGS
    for ( ifilt=0; ifilt < SNDATA_FILTER.NDEF; ifilt++ ) {
      ifilt_obs  = SNDATA_FILTER.MAP[ifilt];
      sprintf(parName,"%s_MAG_%c", PREFIX, FILTERSTRING[ifilt_obs] );
      LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
      WR_SNFITSIO_TABLEVAL[itype].value_1E = 
	SNDATA.HOSTGAL_MAG[igal][ifilt] ;
      wr_snfitsio_fillTable ( ptrColnum, parName, itype );
    }

    // HOSTGAL MAGERR (Feb 2019)
    for ( ifilt=0; ifilt < SNDATA_FILTER.NDEF; ifilt++ ) {
      ifilt_obs  = SNDATA_FILTER.MAP[ifilt];
      sprintf(parName,"%s_MAGERR_%c", PREFIX, FILTERSTRING[ifilt_obs] );
      LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
      WR_SNFITSIO_TABLEVAL[itype].value_1E = 
	SNDATA.HOSTGAL_MAGERR[igal][ifilt] ;
      wr_snfitsio_fillTable ( ptrColnum, parName, itype );
    }

  } // end igal

  // SN-dependent properties related to host which do not depend on igal

  // HOSTGAL SB, FLUXCAL/arcsec^2 (Sep 2 2014)
  for ( ifilt=0; ifilt < SNDATA_FILTER.NDEF; ifilt++ ) {
    ifilt_obs  = SNDATA_FILTER.MAP[ifilt];
    sprintf(parName,"HOSTGAL_SB_FLUXCAL_%c", FILTERSTRING[ifilt_obs] );
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = 
      SNDATA.HOSTGAL_SB_FLUXCAL[ifilt] ;
    wr_snfitsio_fillTable ( ptrColnum, parName, itype );
  }

  // host confusion
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  sprintf(parName,"HOSTGAL_CONFUSION" );
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.HOSTGAL_CONFUSION ;
  wr_snfitsio_fillTable ( ptrColnum, parName, itype );


  // -------------- END HOST ----------

  // PEAKMJD 
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SEARCH_PEAKMJD ;
  wr_snfitsio_fillTable ( ptrColnum, "PEAKMJD", itype );

  // SEARCH_TYPE (for SDSS only)
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1J = SNDATA.SEARCH_TYPE ;
  wr_snfitsio_fillTable ( ptrColnum, "SEARCH_TYPE", itype );


  // optional PRIVATE variables
  for ( ivar=1; ivar <= SNDATA.NVAR_PRIVATE ; ivar++ ) {
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1D = SNDATA.PRIVATE_VALUE[ivar] ;
    sprintf(parName,"%s" ,SNDATA.PRIVATE_KEYWORD[ivar] );
    wr_snfitsio_fillTable ( ptrColnum, parName, itype );
  }


  // --------------------------------------------
  if ( !SNFITSIO_SIMFLAG_SNANA ) { return ; }
  // --------------------------------------------

  // write the simulated model for reach SN to allow SN-mixtures
  // of different SN types.
  

  // SIM_MODEL name (e.g., "SALT2", "mlcs2k2")
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_A = SNDATA.SIM_MODEL_NAME ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_MODEL_NAME", itype );

  // SIM_MODEL index 
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1I = (short)SNDATA.SIM_MODEL_INDEX ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_MODEL_INDEX", itype );

  // SIM_TYPE index (added Jun 9 2013)
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1I = (short)SNDATA.SIM_TYPE_INDEX ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_TYPE_INDEX", itype );

  // SIM_TYPE name  (Ia, Ib, II ...)
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_A = SNDATA.SIM_TYPE_NAME ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_TYPE_NAME", itype );


  // NON1a index (0 => 1a)
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1J = SNDATA.SIM_TEMPLATE_INDEX ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_TEMPLATE_INDEX", itype );

  // SIMLIB LIBID
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1J = SNDATA.SIM_LIBID ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_LIBID", itype );

  // Now many times this LIBID was generated
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1J = SNDATA.SIM_NGEN_LIBID ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_NGEN_LIBID", itype );

  // Number of simulated observations suppresed because
  // model is not defined (Mar 2017)
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1J = SNDATA.SIM_NOBS_UNDEFINED ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_NOBS_UNDEFINED", itype );

  // SIM_SEARCHEFF_MASK
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1J = SNDATA.SIM_SEARCHEFF_MASK ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_SEARCHEFF_MASK", itype );

  // -------
  // SIM_REDSHIFT
  // zhelio 
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_REDSHIFT_HELIO ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_REDSHIFT_HELIO", itype );

  // zcmb
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_REDSHIFT_CMB ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_REDSHIFT_CMB", itype );

  // zhost (different from zhelio if wrong host)
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_REDSHIFT_HOST ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_REDSHIFT_HOST", itype );

  // integer redshift flag to indicate source of redshift
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1I = SNDATA.SIM_REDSHIFT_FLAG ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_REDSHIFT_FLAG", itype );

  // true HOSTGAL OBJID
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1K = SNDATA.SIM_HOSTLIB_GALID ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_HOSTLIB_GALID", itype );

  // Vpec
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_VPEC ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_VPEC", itype );

  // - - - - - - - user-selected sim host properties - - - - - - - -

  // selected HOSTLIB properties using sim-input key  HOSTLIB_STOREVAR
  for(ipar=0; ipar < SNDATA.NPAR_SIM_HOSTLIB; ipar++ ) {
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_HOSTLIB_PARVAL[ipar] ;
    sprintf(parName,"%s", SNDATA.SIM_HOSTLIB_KEYWORD[ipar] );
    wr_snfitsio_fillTable ( ptrColnum, parName, itype );
  }
  

  // -------------------
  // SIM_DLMU
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_DLMU ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_DLMU", itype );

  // SIM_LENSDMU
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_LENSDMU ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_LENSDMU", itype );

  // SIM_RA/DEC
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1D = SNDATA.SIM_RA ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_RA", itype );

  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1D = SNDATA.SIM_DEC ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_DEC", itype );

  // SIM_MWEBV
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_MWEBV ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_MWEBV", itype );

  // SIM_PEAKMJD
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_PEAKMJD ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_PEAKMJD", itype );

  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_MAGSMEAR_COH ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_MAGSMEAR_COH", itype );


  // Ju 16 2016: always write SIM_RV & SIM_AV
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_AV ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_AV", itype );
  
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_RV ;
  wr_snfitsio_fillTable ( ptrColnum, "SIM_RV", itype );


  if ( SNDATA.SIM_MODEL_INDEX == MODEL_SALT2 ) {
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_SALT2x0 ;
    wr_snfitsio_fillTable ( ptrColnum, "SIM_SALT2x0", itype );

    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_SALT2x1 ;
    wr_snfitsio_fillTable ( ptrColnum, "SIM_SALT2x1", itype );

    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_SALT2c ;
    wr_snfitsio_fillTable ( ptrColnum, "SIM_SALT2c", itype );

    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_SALT2mB ;
    wr_snfitsio_fillTable ( ptrColnum, "SIM_SALT2mB", itype );

    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_SALT2alpha ;
    wr_snfitsio_fillTable ( ptrColnum, "SIM_SALT2alpha", itype );

    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_SALT2beta ;
    wr_snfitsio_fillTable ( ptrColnum, "SIM_SALT2beta", itype );

    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_SALT2gammaDM ;
    wr_snfitsio_fillTable ( ptrColnum, "SIM_SALT2gammaDM", itype );
    
  }

  if ( SNDATA.SIM_MODEL_INDEX  == MODEL_MLCS2k2 ) {
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_DELTA ;
    wr_snfitsio_fillTable ( ptrColnum, "SIM_DELTA", itype );

  }

  if ( SNDATA.SIM_MODEL_INDEX  == MODEL_SNOOPY ) {
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_STRETCH ;
    wr_snfitsio_fillTable ( ptrColnum, "SIM_STRETCH", itype );
  }


  if ( SNDATA.SIM_MODEL_INDEX  == MODEL_NON1ASED ||
       SNDATA.SIM_MODEL_INDEX  == MODEL_NON1AGRID ) {
  }


  if ( SNDATA.SIM_MODEL_INDEX  == MODEL_SIMSED && SNFITSIO_SIMFLAG_MODELPAR ) {
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_SALT2x0 ;
    wr_snfitsio_fillTable ( ptrColnum, "SIMSED_SALT2x0", itype );

    for ( ipar=0; ipar < SNDATA.NPAR_SIMSED; ipar++ ) {
      LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
      WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIMSED_PARVAL[ipar] ;
      sprintf(parName,"%s" ,SNDATA.SIMSED_KEYWORD[ipar] );
      wr_snfitsio_fillTable ( ptrColnum, parName, itype );
    }
  }


  if ( SNDATA.SIM_MODEL_INDEX  == MODEL_BYOSED ||
       SNDATA.SIM_MODEL_INDEX  == MODEL_SNEMO ) {
    for ( ipar=0; ipar < SNDATA.NPAR_PySEDMODEL;  ipar++ ) {
      LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
      WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.PySEDMODEL_PARVAL[ipar] ;
      sprintf(parName,"%s" ,SNDATA.PySEDMODEL_KEYWORD[ipar] );
      wr_snfitsio_fillTable ( ptrColnum, parName, itype );
    }
  }

  if ( SNDATA.SIM_MODEL_INDEX  == MODEL_LCLIB && SNFITSIO_SIMFLAG_MODELPAR) {
    for ( ipar=0; ipar < SNDATA.NPAR_LCLIB;  ipar++ ) {
      LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
      WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.LCLIB_PARVAL[ipar] ;
      sprintf(parName,"%s" ,SNDATA.LCLIB_KEYWORD[ipar] );
      wr_snfitsio_fillTable ( ptrColnum, parName, itype );
    }
  }


  // now do filter-dependent stuf

  // PEAKMAG
  for ( ifilt=0; ifilt < SNDATA_FILTER.NDEF; ifilt++ ) {
    ifilt_obs  = SNDATA_FILTER.MAP[ifilt];
    sprintf(parName,"SIM_PEAKMAG_%c", FILTERSTRING[ifilt_obs] );
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_PEAKMAG[ifilt_obs] ;
    wr_snfitsio_fillTable ( ptrColnum, parName, itype );
  }

  // TEMPLATE MAG for LCLIB model only
  if ( SNDATA.SIM_MODEL_INDEX  == MODEL_LCLIB ) {
    for ( ifilt=0; ifilt < SNDATA_FILTER.NDEF; ifilt++ ) {
      ifilt_obs  = SNDATA_FILTER.MAP[ifilt];
      sprintf(parName,"SIM_TEMPLATEMAG_%c", FILTERSTRING[ifilt_obs] );
      LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
      WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_TEMPLATEMAG[ifilt_obs];
      wr_snfitsio_fillTable ( ptrColnum, parName, itype );
    }
  }

  // EXPOSURE times
  for ( ifilt=0; ifilt < SNDATA_FILTER.NDEF; ifilt++ ) {
    ifilt_obs  = SNDATA_FILTER.MAP[ifilt];
    sprintf(parName,"SIM_EXPOSURE_%c", FILTERSTRING[ifilt_obs] );
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_EXPOSURE_TIME[ifilt_obs];
    wr_snfitsio_fillTable ( ptrColnum, parName, itype );
  }

  // GALFRAC
  if ( SNDATA.SIM_HOSTLIB_MSKOPT ) {
    for ( ifilt=0; ifilt < SNDATA_FILTER.NDEF; ifilt++ ) {
      ifilt_obs  = SNDATA_FILTER.MAP[ifilt];
      sprintf(parName,"SIM_GALFRAC_%c", FILTERSTRING[ifilt_obs] );      
      LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
      WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_GALFRAC[ifilt_obs] ;
      wr_snfitsio_fillTable ( ptrColnum, parName, itype );
    }
  }


  // strong lens params (Jul 2019)
  if ( SNDATA.SIM_SL_FLAG ) { 
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1J = SNDATA.SIM_SL_IDLENS ;
    wr_snfitsio_fillTable ( ptrColnum, "SIM_STRONGLENS_ID", itype );

    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_SL_zLENS ;
    wr_snfitsio_fillTable ( ptrColnum, "SIM_STRONGLENS_z", itype );

    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_SL_TDELAY ;
    wr_snfitsio_fillTable ( ptrColnum, "SIM_STRONGLENS_TDELAY", itype );

    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIM_SL_MAGSHIFT ;
    wr_snfitsio_fillTable ( ptrColnum, "SIM_STRONGLENS_MAGSHIFT", itype );

    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1I = SNDATA.SIM_SL_NIMG ;
    wr_snfitsio_fillTable ( ptrColnum, "SIM_STRONGLENS_NIMG", itype );

    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1I = SNDATA.SIM_SL_IMGNUM ;
    wr_snfitsio_fillTable ( ptrColnum, "SIM_STRONGLENS_IMGNUM", itype );
  }


  //  if ( SNFITSIO_NSUBSAMPLE_MARK > 1 ) {
  sprintf(parName,"SIM_SUBSAMPLE_INDEX");
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1I = SNDATA.SUBSAMPLE_INDEX ;
  wr_snfitsio_fillTable ( ptrColnum, parName, itype );

  // make sure that required keys exist.
  check_required_headkeys();

  return ;

} // end of wr_snfitsio_update_head


// =======================
void wr_snfitsio_fillTable(int *COLNUM, char *parName, int itype ) {

  // find column number for *parName and fill fits table.
  // If *COLNUM is < 0 then do the brute force character
  // search for *parName; otherwise use *COLNUM as an 
  // index to get the column number.
  //
  // Aug 25 2017: ABORT if char string is blank 
  //    (to avoid confusing abort later on read-back)
  //
  // Sep 20 2017: define logical ALLOW_BLANK to allow exceptions
  //              for the no-blank rule on strins. See SUBSURVEY.
  //
  int istat, colnum, firstelem, firstrow, nrow, LEN ;
  fitsfile *fp ;
  char *ptrForm, cfirst[2], clast[2] ;
  char fnam[] = "wr_snfitsio_fillTable";

  // ------------ BEGIN -----------

  fp    = fp_snfitsFile[itype] ;

  if ( *COLNUM < 0 ) 
    { *COLNUM = IPAR_SNFITSIO(1,parName,itype); }

  colnum    = *COLNUM ;
  nrow      = 1 ;
  firstelem = 1 ;
  firstrow  = WR_SNFITSIO_TABLEVAL[itype].NROW ;
  ptrForm   = SNFITSIO_TABLEDEF[itype].ptrForm[colnum];
  
  LEN = strlen(ptrForm);
  sprintf(cfirst, "%c",  ptrForm[0] ); // first character only
  sprintf(clast,  "%c",  ptrForm[LEN-1] ); // last char only

  istat     = 0 ;

  /*
  printf(" %2d : xxxx colnum = %2d  clast = %s (parName=%s) \n", 
	 NWR_SNFITSIO, colnum, clast, parName );
  */

  if ( strcmp(clast,"A") == 0 ) {
    char *A = WR_SNFITSIO_TABLEVAL[itype].value_A ;
    int ALLOW_BLANK = ( strcmp(parName,"SUBSURVEY")==0 );

    if ( strlen(A) == 0 &&  !ALLOW_BLANK  ) {
      sprintf(c1err,"Cannot write %s='' (blank string)", parName);
      sprintf(c2err,"to colnum=%d of table=%s", colnum, snfitsType[itype]);
      errmsg(SEV_FATAL, 0, fnam, c1err, c2err ); 
    }
    fits_write_col(fp, TSTRING, colnum, firstrow, firstelem, nrow,
		   &WR_SNFITSIO_TABLEVAL[itype].value_A, &istat);  
  }
  else if ( strcmp(ptrForm,"1D") == 0 ) {
    fits_write_col(fp, TDOUBLE, colnum, firstrow, firstelem, nrow,
		   &WR_SNFITSIO_TABLEVAL[itype].value_1D, &istat);  
  }
  else if ( strcmp(ptrForm,"1E") == 0 ) {
    fits_write_col(fp, TFLOAT, colnum, firstrow, firstelem, nrow,
		   &WR_SNFITSIO_TABLEVAL[itype].value_1E, &istat);  
  }
  else if ( strcmp(ptrForm,"1J") == 0 ) {  // 32-bit signed int
    fits_write_col(fp, TINT, colnum, firstrow, firstelem, nrow,
		   &WR_SNFITSIO_TABLEVAL[itype].value_1J, &istat);  
  }
  else if ( strcmp(ptrForm,"1I") == 0 ) {  // 16-bit unsigned int
    fits_write_col(fp, TSHORT, colnum, firstrow, firstelem, nrow,
		   &WR_SNFITSIO_TABLEVAL[itype].value_1I, &istat);  
  }
  else if ( strcmp(ptrForm,"1K") == 0 ) {  // 64 bit long long
    fits_write_col(fp, TLONGLONG, colnum, firstrow, firstelem, nrow,
		   &WR_SNFITSIO_TABLEVAL[itype].value_1K, &istat);  
  }


  else {
    sprintf(c1err,"Unrecognized Form = '%s' for param='%s' ", 
	    ptrForm, parName) ;
    sprintf(c2err,"%s", "Check valid forms in cfitsio guide.");
    errmsg(SEV_FATAL, 0, fnam, c1err, c2err ); 
  }

  sprintf(BANNER,"fits_write_col for %s-param: %s", 
	  snfitsType[itype], parName );

  snfitsio_errorCheck(BANNER, istat);

  return ;

} //  end of wr_snfitsio_fillTable



// ====================================
void wr_snfitsio_update_phot(int ep) {

  int itype, LOC ,*ptrColnum  ;
  int WRFULL = ( SNFITSIO_COMPACT_FLAG == false );
  char fnam[] = "wr_snfitsio_update_phot" ;
  
  // ------------- BEGIN --------------

  LOC = 0;
  itype = ITYPE_SNFITSIO_PHOT ;
  WR_SNFITSIO_TABLEVAL[itype].NROW++ ;

  // MJD
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1D = SNDATA.MJD[ep] ;
  wr_snfitsio_fillTable ( ptrColnum, "MJD", itype );
 
  // FILTER
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_A = SNDATA.FILTCHAR[ep] ;
  wr_snfitsio_fillTable ( ptrColnum, "BAND", itype );

  // CCDNUM (Mar 2021)
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1I = (short int)SNDATA.CCDNUM[ep] ;
  wr_snfitsio_fillTable ( ptrColnum, "CCDNUM", itype );
  
  // FIELD
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_A = SNDATA.FIELDNAME[ep] ;
  wr_snfitsio_fillTable ( ptrColnum, "FIELD", itype );

  // PHOTFLAG & PHOTPROB
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1J = SNDATA.PHOTFLAG[ep] ;
  wr_snfitsio_fillTable ( ptrColnum, "PHOTFLAG", itype );

  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.PHOTPROB[ep] ;
  wr_snfitsio_fillTable ( ptrColnum, "PHOTPROB", itype );

  // FLUXCAL and its error
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.FLUXCAL[ep] ;
  wr_snfitsio_fillTable ( ptrColnum, "FLUXCAL", itype );

  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.FLUXCAL_ERRTOT[ep] ;
  wr_snfitsio_fillTable ( ptrColnum, "FLUXCALERR", itype );

  if ( SNDATA.NEA_PSF_UNIT ) {
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.PSF_NEA[ep] ;
    wr_snfitsio_fillTable ( ptrColnum, "PSF_NEA", itype );
  }
  else {
    // PSF (SIG1, SIG2 and ratio). Unit is Gauss-sigma, pixels
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.PSF_SIG1[ep] ;
    wr_snfitsio_fillTable ( ptrColnum, "PSF_SIG1", itype );
    
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.PSF_SIG2[ep] ;
    wr_snfitsio_fillTable ( ptrColnum, "PSF_SIG2", itype );

    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.PSF_RATIO[ep] ;
    wr_snfitsio_fillTable ( ptrColnum, "PSF_RATIO", itype );
  }

  // SKYSIG (ADU per pixel)


  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SKY_SIG[ep] ;
  wr_snfitsio_fillTable ( ptrColnum, "SKY_SIG", itype );

  if ( WRFULL ) {
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SKY_SIG_T[ep] ;
    wr_snfitsio_fillTable ( ptrColnum, "SKY_SIG_T", itype );
    
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.READNOISE[ep] ;
    wr_snfitsio_fillTable ( ptrColnum, "RDNOISE", itype );
  }

  // zeropt and it error and the GAIN (e- per ADU)
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.ZEROPT[ep] ;
  wr_snfitsio_fillTable ( ptrColnum, "ZEROPT", itype );


  if ( WRFULL ) {
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.ZEROPT_ERR[ep] ;
    wr_snfitsio_fillTable ( ptrColnum, "ZEROPT_ERR", itype );
    
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.GAIN[ep] ;
    wr_snfitsio_fillTable ( ptrColnum, "GAIN", itype );
  }

  if ( SNDATA.NXPIX > 0 ) {
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.XPIX[ep] ;
    wr_snfitsio_fillTable ( ptrColnum, "XPIX", itype );

    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.YPIX[ep] ;
    wr_snfitsio_fillTable ( ptrColnum, "YPIX", itype );
  }

  // check for sim-mag

  if ( SNFITSIO_SIMFLAG_SNANA || SNFITSIO_SIMFLAG_MAGOBS ) { 
    LOC++ ; 
    ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIMEPOCH_MAG[ep] ;
    wr_snfitsio_fillTable ( ptrColnum, "SIM_MAGOBS", itype );
  }


  if ( SNFITSIO_SIMFLAG_SNANA && WRFULL ) { 
    LOC++ ; 
    ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIMEPOCH_FLUXCAL_HOSTERR[ep];
    wr_snfitsio_fillTable ( ptrColnum, "SIM_FLUXCAL_HOSTERR", itype );
  }

  if ( SNFITSIO_SIMFLAG_SNRMON ) {  // Mar 28 2018
    LOC++ ; 
    ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = SNDATA.SIMEPOCH_SNRMON[ep];
    wr_snfitsio_fillTable ( ptrColnum, SNDATA.VARNAME_SNRMON, itype );
  }

  return ;

} // end of  wr_snfitsio_update_phot


// =================================================
void  wr_snfitsio_update_spec(int imjd)  {

  // Aug 2016:
  // Update SPEC.FITS file with spectra.
  // Mar 2019: write optional SIM_WARP column as 1000*WARP
  // Feb 2021: write GENFLAM for sim

  int  NBLAM_TOT = GENSPEC.NBLAM_TOT ;
  int  NBLAM_WR  = GENSPEC.NBLAM_VALID[imjd] ;
  int  itype, LOC ,*ptrColnum, PTRSPEC_MIN, PTRSPEC_MAX   ;
  //  char fnam[] = "wr_snfitsio_update_spec" ;

  // ----------- BEGIN ------------

  // Bail if no spectrum (e.g, sim outside Trest range)
  if ( GENSPEC.SKIP[imjd] ) { return; }

  // xxx  if ( NBLAM_WR == 0 ) { return ; } 

  // calculate obs-pointer for photometry fits file.
  PTRSPEC_MIN = WR_SNFITSIO_TABLEVAL[ITYPE_SNFITSIO_SPECTMP].NROW+1 ;
  PTRSPEC_MAX = PTRSPEC_MIN - 1 + NBLAM_WR ;

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
  // update summary table (one row per spectrum)
  itype = ITYPE_SNFITSIO_SPEC ;
  LOC = 0 ;
  WR_SNFITSIO_TABLEVAL[itype].NROW++ ;

  // SNID 
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_A = SNDATA.CCID ;
  wr_snfitsio_fillTable ( ptrColnum, "SNID", itype );  

  // MJD  
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1D = GENSPEC.MJD_LIST[imjd] ;
  wr_snfitsio_fillTable ( ptrColnum, "MJD", itype );

  // Texpose
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = GENSPEC.TEXPOSE_LIST[imjd] ;
  wr_snfitsio_fillTable ( ptrColnum, "Texpose", itype );

  // - - - - - -
  // SNR_COMPUTE
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = GENSPEC.SNR_COMPUTE_LIST[imjd] ;
  wr_snfitsio_fillTable ( ptrColnum, "SNR_COMPUTE", itype );

  // LAMOBS range for SNR_COMPUTE
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = GENSPEC.LAMOBS_SNR_LIST[imjd][0] ;
  wr_snfitsio_fillTable ( ptrColnum, "LAMMIN_SNR", itype );

  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1E = GENSPEC.LAMOBS_SNR_LIST[imjd][1] ;
  wr_snfitsio_fillTable ( ptrColnum, "LAMMAX_SNR", itype );

  // - - - - 
  // NBLAM
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1I = NBLAM_WR ;
  wr_snfitsio_fillTable ( ptrColnum, "NBIN_LAM", itype );

  // PTRSPEC_MIN
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1J = PTRSPEC_MIN ;
  wr_snfitsio_fillTable ( ptrColnum, "PTRSPEC_MIN", itype );

  // PTRSPEC_MAX
  LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
  WR_SNFITSIO_TABLEVAL[itype].value_1J = PTRSPEC_MAX ;
  wr_snfitsio_fillTable ( ptrColnum, "PTRSPEC_MAX", itype );


  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
  // update spectrum table
  int    ilam, ILAM ;
  double GENFLAM, GENMAG, FLAM, FLAMERR, WARP ;

  itype = ITYPE_SNFITSIO_SPECTMP ;

  // loop over all lambda bins, but only write out ones with defined flux.
  for(ilam=0; ilam <= NBLAM_TOT; ilam++ ) {

    if ( ilam < NBLAM_TOT ) {
      ILAM = ilam ;
      GENFLAM    = GENSPEC.GENFLAM_LIST[imjd][ilam];
      GENMAG     = GENSPEC.GENMAG_LIST[imjd][ilam];
      FLAM       = GENSPEC.FLAM_LIST[imjd][ilam];
      FLAMERR    = GENSPEC.FLAMERR_LIST[imjd][ilam];
      WARP       = GENSPEC.FLAMWARP_LIST[imjd][ilam];
      if ( WARP > 30.0 ) { WARP = 30.0; } // avoid I*2 overflow
    }
    else {
      // end-of-event marker
      ILAM = 777 ;
      GENFLAM = 1.0; GENMAG=0.0; WARP=1.0;
      FLAM = FLAMERR = SNFITSIO_EOE_MARKER ;
    }
    if ( FLAMERR <= 0.0 ) { continue ; } // skip unphysical values  

    LOC=0;
    WR_SNFITSIO_TABLEVAL[ITYPE_SNFITSIO_SPECTMP].NROW++ ;

    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1I = ILAM ;
    wr_snfitsio_fillTable ( ptrColnum, "LAMINDEX", itype );  

    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = FLAM ;
    wr_snfitsio_fillTable ( ptrColnum, "FLAM", itype );  
   
    LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
    WR_SNFITSIO_TABLEVAL[itype].value_1E = FLAMERR ;
    wr_snfitsio_fillTable ( ptrColnum, "FLAMERR", itype );  

    if ( SNFITSIO_SIMFLAG_SNANA ) {
      LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
      WR_SNFITSIO_TABLEVAL[itype].value_1E = GENFLAM ;
      wr_snfitsio_fillTable ( ptrColnum, "SIM_FLAM", itype );  

      /*
      LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
      WR_SNFITSIO_TABLEVAL[itype].value_1I = (int)(GENMAG*100.0 + 0.5) ;
      wr_snfitsio_fillTable ( ptrColnum, "SIM_MAG", itype );  
      */
    }

    if ( GENSPEC.USE_WARP ) {
      LOC++ ; ptrColnum = &WR_SNFITSIO_TABLEVAL[itype].COLNUM_LOOKUP[LOC] ;
      WR_SNFITSIO_TABLEVAL[itype].value_1I = (int)(WARP*1000.0 + 0.5) ;
      wr_snfitsio_fillTable ( ptrColnum, "SIM_WARP", itype ) ;  
    }

  } // end ilam

  return ;

} // end wr_snfitsio_update_spec


// ==================================
int IPAR_SNFITSIO(int OPT, char *parName, int itype) {

  // return IPAR header-column index for *parName and *type.
  // OPT=0 => do NOT abort on error, but return -9
  // OPT=1 => abort on error;

  int   ipar, NPAR ;
  char *ptrTmp;
  char fnam[] = "IPAR_SNFITSIO" ;

  // ------------ BEGIN -----------

  NPAR = NPAR_SNFITSIO[itype] ;
  for ( ipar=1; ipar <= NPAR; ipar++ ) {
    ptrTmp = SNFITSIO_TABLEDEF[itype].name[ipar] ;

    if ( strcmp(ptrTmp,parName) == 0 ) 
      { return ipar ; }
  }

  // if we get here then abort or return -9. 
  if ( OPT == 0 )
    { return -9 ; }
  else {
    sprintf(c1err, "Could not find IPAR for parName='%s'", parName);
    sprintf(c2err, "Check parameter names in %s ", 
	    snfitsFile[IFILE_SNFITSIO][itype]);
    errmsg(SEV_FATAL, 0, fnam, c1err, c2err ); 
    return -9 ;
  }

}  // end of IPAR_SNFITSIO

// ==================================
int IPARFORM_SNFITSIO(int OPT, int iform, char *parName, int itype) {

  // same as IPAR_SNFITSIOT, but search subset with form (1J,1E,1D ...)
  // specified by the input index 'iform'.
  // OPT=0 => do NOT abort on error, but return -9
  // OPT=1 => abort on error;

  int ipar, NPAR, icol ;
  char *ptrTmp;
  char fnam[] = "IPARFORM_SNFITSIO";

  // ------------ BEGIN -----------

  NPAR = RD_SNFITSIO_TABLEVAL[itype].NPAR[iform]; 

  for ( ipar=1; ipar <= NPAR; ipar++ ) {

    // get absolute column index
    icol  = RD_SNFITSIO_TABLEVAL[itype].IPAR[iform][ipar] ; 

    // parameter name at this column
    ptrTmp = SNFITSIO_TABLEDEF[itype].name[icol] ;

    if ( strcmp(ptrTmp,parName) == 0 )  { return ipar ; }
  }
  
  // if we get here then abort or return -9.
  if ( OPT == 0 )
    { return -9 ; }
  else {
    sprintf(c1err, "Could not find IPAR(iform=%d) for parName='%s'", 
	    iform, parName);
    sprintf(c2err, "Check parameter names in %s ", 
	    snfitsFile[IFILE_SNFITSIO][itype]);
    errmsg(SEV_FATAL, 0, fnam, c1err, c2err ); 
    return -9 ;
  }

}  // end of IPARFORM_SNFITSIO

// ==================================
void WR_SNFITSIO_END(void) {

  int istat, extver, ifile, itype, NTYPE, isys ;
  fitsfile *fp ;

  // ------------ BEGIN -------------

  NTYPE = 2 ; // defult is HEAD + PHOT

  if ( SNFITSIO_SIMFLAG_SPECTROGRAPH ) { 
    NTYPE += 2 ; 
        
    // append flux-table after summary table so that it's
    // all in one file. Then delete SPECTMP flux-table.
    extver = istat=0;
    fp     = fp_snfitsFile[ITYPE_SNFITSIO_SPECTMP];
    fits_movnam_hdu( fp, BINARY_TBL, "SPECTRO_FLUX", extver, &istat);
    fits_copy_hdu(fp_snfitsFile[ITYPE_SNFITSIO_SPECTMP],
		  fp_snfitsFile[ITYPE_SNFITSIO_SPEC], 0, &istat) ;
    sprintf(c1err, "Append SPEC file" );
    snfitsio_errorCheck(c1err, istat);  
  }

  for ( itype=0; itype < NTYPE; itype++ ) {
    fp    = fp_snfitsFile[itype];
    istat = 0 ;
    fits_close_file(fp, &istat);
    sprintf(c1err, "Close %s-FITS file", snfitsType[itype] );
    snfitsio_errorCheck(c1err, istat);
  }

  if ( SNFITSIO_SIMFLAG_SPECTROGRAPH ) { 
    // remove SPECTMP file after its table has been
    // append to SPEC file with summary table.
    char cmd[400];    
    ifile = IFILE_SNFITSIO ;
    itype = ITYPE_SNFITSIO_SPECTMP ;
    sprintf(cmd,"rm %s", snfitsFile_plusPath[ifile][itype] );
    //printf(" xxx '%s' \n", cmd) ;
    isys = system( cmd );
  }

} // end of WR_SNFITSIO_END

void wr_snfitsio_end__(void) {
  WR_SNFITSIO_END();
}

// ========================================
void snfitsio_close(int ifile, int itype) {

  int istat ;
  fitsfile *fp ;
  //  char fnam[] = "snfitsio_close" ; 

  // ------------ BEGIN -----------

  istat = 0 ;
  fp = fp_snfitsFile[itype] ;
  fits_close_file(fp, &istat);
  sprintf(c1err,"Close %s ", snfitsFile[ifile][itype] );
  snfitsio_errorCheck(c1err,istat);

} // end of  snfitsio_close

// ========================================
void snfitsio_errorCheck(char *comment, int status) {

  char fnam[] = "snfitsio_errorCheck" ;

  /*****************************************************/
  /* Print out cfitsio error messages and exit program */
  /*****************************************************/

  if (status) {
    fits_report_error(stderr, status); /* print error report */
    errmsg(SEV_FATAL, 0, fnam, comment, "Check cfitsio routines." ); 
  }
  return;
}


// ###########################################
//
//   Below are the READ-BACK UTILITIES
//
// ###########################################

void RD_SNFITSIO_INIT(int init_num) {

  // Created Feb 2021; one-time init
  // init_num = 1 --> first init --> init everything  
  // init_sum = 2 --> 2nd init; RD_SNFITSTIO_INIT already called 

  NFILE_SNFITSIO           = 0 ;
  NSNLC_SNFITSIO_TOT       = 0 ;
  SNFITSIO_PHOT_VERSION[0] = 0 ;
  SNFITSIO_DATA_PATH[0]    = 0 ;

  if ( init_num == 1 ) {
    init_SNDATA_GLOBAL();
    init_GENSPEC_GLOBAL();
  }

} // end RD_SNFITSIO_INIT

// =========================================
int RD_SNFITSIO_PREP(int MSKOPT, char *PATH, char *version) {

  //
  // open/read/init fits files for this *version.
  // Returns number of SN on sucess;
  // Returns -1 if this  version is NOT in fits format.
  //
  // MSKOPT == 0 : read each header file to count NSN, then
  //               open first PHOT file.
  //
  // MSKOPT & 1 : return istat after checking if in fits-format
  //              ==> don't open/read the tables
  //
  // MSKOPT & 2 : read header only; do NOT open first PHOT file
  //
  // PATH = optional user-path to data; 
  //        if PATH="", use default SNDATA_ROOT/lcmerge


  int istat, ifile, ep ;
  char fnam[] = "RD_SNFITSIO_PREP" ;

  // ------------- BEGIN -----------

  sprintf(SNFITSIO_PHOT_VERSION, "%s", version);
  sprintf(SNFITSIO_DATA_PATH, "%s", PATH);
  
  istat = 
    getInfo_PHOTOMETRY_VERSION( SNFITSIO_PHOT_VERSION    // (I)
				,SNFITSIO_DATA_PATH      // (O)
				,SNFITSIO_LISTFILE       // (O)
				,SNFITSIO_READMEFILE     // (O)
				);

  // abort if can't find version
  if ( istat == ERROR ) { 
    sprintf(c1err,"Cannot find SNANA VERSION '%s'", version);
    sprintf(c2err,"   ");
    errmsg(SEV_FATAL, 0, fnam, c1err, c2err); 
    return istat ;  
  }


  // read list of fits files.
  istat = rd_snfitsio_list();

  if ( istat < 0 ) { return istat ; } // not in FITS format

  if ( (MSKOPT & 1)>0 ) { // check if FITS format; don't read
    MALLOC_LEN_SNFITSIO[ITYPE_SNFITSIO_HEAD] = 0 ;
    MALLOC_LEN_SNFITSIO[ITYPE_SNFITSIO_PHOT] = 0 ;
    return istat ; 
  }

  // print summary
  printf("  ###################################################### \n");
  printf("  %s: \n", fnam);
  printf("  Prepare to read PHOTOMETRY VERSION '%s' from \n", 
	 SNFITSIO_PHOT_VERSION );
  printf("\t %s \n", 
	 SNFITSIO_DATA_PATH );
  printf("  ###################################################### \n");
  fflush(stdout);

  IFILE_SNFITSIO = 0 ;
  NSNLC_SNFITSIO_TOT = 0 ;
  for ( ifile=0; ifile < MXFILE_SNFITSIO; ifile++ )  { 
    NSNLC_SNFITSIO[ifile]     = 0 ; 
    NSNLC_SNFITSIO_SUM[ifile] = 0 ; 
  }


  // set default mask to read all epochs
  NEP_RDMASK_SNFITSIO_PARVAL = 0;
  for ( ep=0; ep < MXEPOCH; ep++ ) 
    {  RDMASK_SNFITSIO_PARVAL[ep] = 1 ; }

  // loop over all header files to get total number of SN.
  // Close each file after reading the NAXIS2 key.

  int photflag_open=0,  vbose=0;
  for (ifile = 1; ifile <= NFILE_SNFITSIO; ifile++ ) {

    rd_snfitsio_open(ifile,photflag_open,vbose); // open and read 

    NSNLC_SNFITSIO_TOT       += NSNLC_SNFITSIO[ifile] ; // increment total
    NSNLC_SNFITSIO_SUM[ifile] = NSNLC_SNFITSIO_TOT ;

    snfitsio_close(ifile, ITYPE_SNFITSIO_HEAD );
  }

  // open and read the first HEADER file ; do not open PHOT file
  if ( (MSKOPT & 2) == 0 ) {  
    IFILE_SNFITSIO    = 1 ;
    ISNFIRST_SNFITSIO = 1 ;               // first ISN in file
    rd_snfitsio_file(IFILE_SNFITSIO);
    rd_snfitsio_specFile(IFILE_SNFITSIO); // check for spectra (4.2019)
  }


  // Feb 2021: init lookup indices for faster read
  int i;
  for(i=0; i < MXPAR_SNFITSIO; i++ ) { 
    SNFITSIO_READINDX_HEAD[i] = -9 ;
    SNFITSIO_READINDX_PHOT[i] = -9 ;
    SNFITSIO_READINDX_SPEC[i] = -9 ;
  }

  return(NSNLC_SNFITSIO_TOT) ;

} // end of RD_SNFITSIO_PREP


void rd_snfitsio_init__(int *init_num) { RD_SNFITSIO_INIT(*init_num); }

int  rd_snfitsio_prep__(int *MSKOPT, char *PATH,  char *version)
{ return RD_SNFITSIO_PREP(*MSKOPT,PATH,version); }

// ========================================================
int RD_SNFITSIO_GLOBAL(char *parName, char *parString) {


  // For input global *parName, return *parString.
  //
  // These globals have already been read from the image-0 header 
  // and here they are transferred to *parString.
  // These globals describe the entire data sample such as 
  // SURVEY-NAME, filter-list, etc ...
  // Note that *parName must match one of the hard-wired
  // names below; if not then it aborts.
  //
  // Dec 31, 2011: add a few more for SIMSED model
  // Nov 25, 2012: check optional PRIVATE_VAR
  // Feb 13, 2014: check SIM_HOSTLIB_NPAR
  // Dec 27, 2015: add SIMLIB_MSKOPT
  // Feb 17, 2017: add SUBSURVEY_FLAG
  // Dec 26, 2018: check for CODE_IVERSION
  // Oct 26, 2020: check for SNANA_VERSION in FITS header
  // Feb 10, 2021: check for SIM_SL_FLAG (strong lens)
  //

  int ipar, ivar ;
  char key[60], tmpString[60];
  char fnam[] = "RD_SNFITSIO_GLOBAL" ;

  // --------------- BEGIN ----------------


  sprintf(tmpString,"NULL");

  if ( strcmp(parName,"SURVEY") == 0 ) {
    sprintf(tmpString,"%s", SNDATA.SURVEY_NAME );
  }
  else if ( strcmp(parName,"SUBSURVEY_FLAG") == 0 ) {
    sprintf(tmpString,"%d", SNDATA.SUBSURVEY_FLAG );
  }
  else if ( strcmp(parName,"FILTERS") == 0 ) {
    sprintf(tmpString,"%s", SNDATA_FILTER.LIST );
  }
  else if ( strcmp(parName,"SPECFILE") == 0 ) {
    sprintf(tmpString,"%s", snfitsFile[1][ITYPE_SNFITSIO_SPEC]);
  }
  else if ( strcmp(parName,"DATATYPE") == 0 ) {
    sprintf(tmpString,"%s", SNDATA.DATATYPE );
  }
  else if ( strcmp(parName,"CODE_IVERSION") == 0 ) {
    sprintf(tmpString,"%d", SNFITSIO_CODE_IVERSION ); // Dec 26 2018
  }
  else if ( strcmp(parName,"SNANA_VERSION") == 0  ) {
    sprintf(tmpString,"%s", SNDATA.SNANA_VERSION ); // Feb 2021
    // xxxx  mark delet  sprintf(tmpString,"%s", SNFITSIO_SNANA_VERSION );
  }
  else if ( strcmp(parName,"SIM_MODEL_NAME") == 0 ) {
    sprintf(tmpString,"%s", SNDATA.SIM_MODEL_NAME );
  }
  else if ( strcmp(parName,"SIM_MODEL_INDEX") == 0 ) {
    sprintf(tmpString,"%d", SNDATA.SIM_MODEL_INDEX );
  }
  else if ( strcmp(parName,"SIM_TYPE_INDEX") == 0 ) {
    sprintf(tmpString,"%d", SNDATA.SIM_TYPE_INDEX );
  }
  else if ( strcmp(parName,"SIMLIB") == 0 ) {
    sprintf(tmpString,"%s", SNDATA.SIMLIB_FILE );
  }
  else if ( strcmp(parName,"SIMLIB_FILE") == 0 ) {
    sprintf(tmpString,"%s", SNDATA.SIMLIB_FILE );
  }
  else if ( strcmp(parName,"SIMLIB_MSKOPT") == 0 ) {
    sprintf(tmpString,"%d", SNDATA.SIMLIB_MSKOPT );
  }
  else if ( strcmp(parName,"SIMOPT_MWCOLORLAW") == 0 ) {
    sprintf(tmpString,"%d", SNDATA.SIMOPT_MWCOLORLAW );
  }
  else if ( strcmp(parName,"SIM_MWRV") == 0 ) {
    sprintf(tmpString,"%f", SNDATA.SIM_MWRV );
  }
  else if ( strcmp(parName,"SIMOPT_MWEBV") == 0 ) {
    sprintf(tmpString,"%d", SNDATA.SIMOPT_MWEBV );
  }
  else if ( strcmp(parName,"SIMSED_NPAR") == 0 ) {
    sprintf(tmpString,"%d", SNDATA.NPAR_SIMSED );
  }

  else if ( strcmp(parName,"BYOSED_NPAR") == 0  || 
	    strcmp(parName,"SNEMO_NPAR" ) == 0  ) {   
    int NPAR = 0;
    if ( strstr(parName,SNDATA.PySEDMODEL_NAME) ) 
      { NPAR = SNDATA.NPAR_PySEDMODEL ; }
    sprintf(tmpString,"%d", NPAR );
  }

  else if ( strcmp(parName,"LCLIB_NPAR") == 0 ) {
    sprintf(tmpString,"%d", SNDATA.NPAR_LCLIB );
  }
  else if ( strcmp(parName,"HOSTLIB_FILE") == 0 ) {
    sprintf(tmpString,"%s", SNDATA.HOSTLIB_FILE );
  }
  else if ( strcmp(parName,"SIM_HOSTLIB_NPAR") == 0 ) {
    sprintf(tmpString,"%d", SNDATA.NPAR_SIM_HOSTLIB );
  }
  else if ( strcmp(parName,"SIM_NSUBSAMPLE_MARK") == 0 ) {
    sprintf(tmpString,"%d", SNFITSIO_NSUBSAMPLE_MARK ); 
  }
  else if ( strcmp(parName,"SIM_VARNAME_SNRMON") == 0 ) {
    sprintf(tmpString,"%s", SNDATA.VARNAME_SNRMON ); 
    if (strlen(SNDATA.VARNAME_SNRMON) > 0 ) 
      { SNFITSIO_SIMFLAG_SNRMON = true ; }
  }
  else if ( strcmp(parName,"SIM_SL_FLAG") == 0 ) {
    sprintf(tmpString,"%d", SNDATA.SIM_SL_FLAG); 
  }
  else if ( strcmp(parName,"NPRIVATE") == 0 ) {
    sprintf(tmpString,"%d", SNDATA.NVAR_PRIVATE );
  }

  // ------------------------------------------
  // check optional PRIVATE_VAR 
  if ( SNDATA.NVAR_PRIVATE > 0 ) {
    for ( ivar = 1; ivar <=  SNDATA.NVAR_PRIVATE; ivar++ ) {
      sprintf(key,"PRIVATE%d", ivar);
      if ( strcmp(parName,key) == 0 ) 
	{  sprintf(tmpString,"%s", SNDATA.PRIVATE_KEYWORD[ivar] );  }
    }
  }

  // ------------------------------------------
  // check optional SIMSED_PAR[ipar] (sim model)
  // Dec 26 2018: note loop is 0 to N to allow legady 1-N labels.
  if ( SNDATA.NPAR_SIMSED > 0  && strstr(parName,"SIMSED")!= NULL ) {

    int NPAR = SNDATA.NPAR_SIMSED;
    int IPAR_START=0, IPAR_END=NPAR-1; // Default as of Dec 26 2018
    if ( SNFITSIO_CODE_IVERSION < 8 ) 
      { IPAR_START=1, IPAR_END=NPAR; } // legacy


    for ( ipar = IPAR_START; ipar <= IPAR_END; ipar++ ) {
      sprintf(key,"SIMSED_PAR%2.2d", ipar);
      if ( strcmp(parName,key) == 0 ) 
	{  sprintf(tmpString, "%s", SNDATA.SIMSED_KEYWORD[ipar] );  }
    }
  }

  // ------------------------------------------
  // Dec 2018: check optional PySEDMODEL_PAR[ipar] 
  if ( SNDATA.NPAR_PySEDMODEL > 0 ) {
    for ( ipar = 0; ipar < SNDATA.NPAR_PySEDMODEL ; ipar++ ) {
      sprintf(key,"%s_PAR%2.2d", SNDATA.PySEDMODEL_NAME, ipar); 
      if ( strcmp(parName,key) == 0 ) 
	{  sprintf(tmpString,"%s", SNDATA.PySEDMODEL_KEYWORD[ipar] );  }
    }
  }

  // ------------------------------------------
  // check optional LCLIB_PAR[ipar] 
  if ( SNDATA.NPAR_LCLIB > 0 ) {
    for ( ipar = 0; ipar < SNDATA.NPAR_LCLIB; ipar++ ) {
      sprintf(key,"LCLIB_PAR%2.2d", ipar);
      if ( strcmp(parName,key) == 0 ) 
	{  sprintf(tmpString,"%s", SNDATA.LCLIB_KEYWORD[ipar] );  }
    }
  }


  // check optional SIMSED_HOSTLIB[ipar] 
  if ( SNDATA.NPAR_SIM_HOSTLIB > 0 ) {
    for ( ipar = 0; ipar <  SNDATA.NPAR_SIM_HOSTLIB; ipar++ ) {
      sprintf(key,"SIM_HOSTLIB_PAR%2.2d", ipar);
      if ( strcmp(parName,key) == 0 ) 
	{  sprintf(tmpString,"%s", SNDATA.SIM_HOSTLIB_KEYWORD[ipar] ); }
    }
  }
  
  // ------------------------------------------
  if ( strcmp(tmpString,"NULL") == 0 ) {
    sprintf(c1err,"Unknown GLOBAL key '%s' ", parName) ;
    sprintf(c2err,"%s", "Check fits header for list of valid keys.");
    errmsg(SEV_FATAL, 0, fnam, c1err, c2err); 
    return(ERROR) ;
  }


  // if we get here then set output *parString
  sprintf(parString,"%s", tmpString);
  return(SUCCESS) ;


} // end of RD_SNFITSIO_GLOBAL

int rd_snfitsio_global__(char *parName, char *parString) 
{ return RD_SNFITSIO_GLOBAL(parName,parString); }


// ==================================================
int RD_SNFITSIO_EVENT(int OPT, int isn) {

  // Created Feb 7 2021
  // Read/store entire event and store in SNDATA struct (sndata.h)
  // OPT &  2 -> read header
  // OPT &  4 -> read photometry/epochs
  // OPT &  8 -> read spec
  // OPT = 14 -> read all

  bool LRD_HEAD = ( OPT & OPTMASK_SNFITSIO_HEAD );
  bool LRD_PHOT = ( OPT & OPTMASK_SNFITSIO_PHOT );
  bool LRD_SPEC = ( OPT & OPTMASK_SNFITSIO_SPEC );
  int  NFILT    = SNDATA_FILTER.NDEF;

  int  j, NRD, igal, NGAL, ifilt, ifilt_obs, ivar, ipar ;
  char PREFIX[20], KEY[40]; 
  double D_OBJID;

  char fnam[] = "RD_SNFITSIO_EVENT";

  // ------------- BEGIN ------------

  if ( LRD_HEAD ) {
    j=0;

    j++ ;  NRD = RD_SNFITSIO_STR(isn, "SUBSURVEY", SNDATA.SUBSURVEY_NAME, 
				 &SNFITSIO_READINDX_HEAD[j] ) ;

    j++ ;  NRD = RD_SNFITSIO_STR(isn, "SNID", SNDATA.CCID, 
				 &SNFITSIO_READINDX_HEAD[j] ) ;
    
    j++ ;  NRD = RD_SNFITSIO_STR(isn, "IAUC", SNDATA.IAUC_NAME, 
				 &SNFITSIO_READINDX_HEAD[j] ) ; 

    j++ ;  NRD = RD_SNFITSIO_INT(isn, "FAKE", &SNDATA.FAKE, 
				 &SNFITSIO_READINDX_HEAD[j] ) ;

    // Mar 2021: allow user mistake of forgetting to set FAKE=1 for fakes 
    if ( SNDATA.FAKE == FAKEFLAG_DATA && SNFITSIO_SIMFLAG_MAGOBS )
      { SNDATA.FAKE = FAKEFLAG_FAKES; }

    if ( !SNFITSIO_SIMFLAG_SNANA ) {
      j++ ;  NRD = RD_SNFITSIO_INT(isn, "MASK_FLUXCOR_SNANA", 
				   &SNDATA.MASK_FLUXCOR, 
				   &SNFITSIO_READINDX_HEAD[j] ) ;
    }

  
    j++ ;  NRD = RD_SNFITSIO_DBL(isn, "RA", &SNDATA.RA, 
				 &SNFITSIO_READINDX_HEAD[j] ) ;

    j++ ;  NRD = RD_SNFITSIO_DBL(isn, "DEC", &SNDATA.DEC, 
				 &SNFITSIO_READINDX_HEAD[j] ) ;

    j++ ;  NRD = RD_SNFITSIO_FLT(isn, "PIXSIZE", &SNDATA.PIXSIZE,
				 &SNFITSIO_READINDX_HEAD[j] ) ;

    j++ ;  NRD = RD_SNFITSIO_INT(isn, "NXPIX", &SNDATA.NXPIX,
				 &SNFITSIO_READINDX_HEAD[j] ) ;

    j++ ;  NRD = RD_SNFITSIO_INT(isn, "NYPIX", &SNDATA.NYPIX,
				 &SNFITSIO_READINDX_HEAD[j] ) ;

    /* xxx mark delete Mar 15 2021 ... read only from PHOT table
    j++ ;  NRD = RD_SNFITSIO_INT(isn, "CCDNUM", &SNDATA.CCDNUM[0], 
				 &SNFITSIO_READINDX_HEAD[j] ) ;
    xxx */

    j++ ;  NRD = RD_SNFITSIO_INT(isn, "SNTYPE", &SNDATA.SNTYPE,
				 &SNFITSIO_READINDX_HEAD[j] ) ;

    j++ ;  NRD = RD_SNFITSIO_INT(isn, "NOBS", &SNDATA.NOBS,
				 &SNFITSIO_READINDX_HEAD[j] ) ;
    SNDATA.NEPOCH = SNDATA.NOBS; // goofy; need to clean up

    j++ ;  NRD = RD_SNFITSIO_FLT(isn, "MWEBV", &SNDATA.MWEBV,
				 &SNFITSIO_READINDX_HEAD[j] ) ;
    j++ ;  NRD = RD_SNFITSIO_FLT(isn, "MWEBV_ERR", &SNDATA.MWEBV_ERR,
				 &SNFITSIO_READINDX_HEAD[j] ) ;

    // ------- redshifts --------
    j++ ;  NRD = RD_SNFITSIO_FLT(isn, "REDSHIFT_HELIO", 
				 &SNDATA.REDSHIFT_HELIO, 
				 &SNFITSIO_READINDX_HEAD[j] ) ;

    j++ ;  NRD = RD_SNFITSIO_FLT(isn, "REDSHIFT_HELIO_ERR", 
				 &SNDATA.REDSHIFT_HELIO_ERR, 
				 &SNFITSIO_READINDX_HEAD[j] ) ;

    j++ ;  NRD = RD_SNFITSIO_FLT(isn, "REDSHIFT_FINAL",  // CMB 
				 &SNDATA.REDSHIFT_FINAL, 
				 &SNFITSIO_READINDX_HEAD[j] ) ;

    j++ ;  NRD = RD_SNFITSIO_FLT(isn, "REDSHIFT_FINAL_ERR", 
				 &SNDATA.REDSHIFT_FINAL_ERR, 
				 &SNFITSIO_READINDX_HEAD[j] ) ;

    // Mar 11 2021: if no REDSHIFT_FINAL, then check for REDSHIFT_CMB column
    if ( NRD == 0 ) {
      j++ ;  NRD = RD_SNFITSIO_FLT(isn, "REDSHIFT_CMB",     
				   &SNDATA.REDSHIFT_FINAL,
				   &SNFITSIO_READINDX_HEAD[j] ) ;

      j++ ;  NRD = RD_SNFITSIO_FLT(isn, "REDSHIFT_CMB_ERR",
				   &SNDATA.REDSHIFT_FINAL_ERR,
				   &SNFITSIO_READINDX_HEAD[j] ) ;
    }

    if ( SNFITSIO_DATAFLAG ) {
      j++ ;  NRD = RD_SNFITSIO_INT(isn, "REDSHIFT_QUALITYFLAG", 
				   &SNDATA.REDSHIFT_QUALITYFLAG, 
				   &SNFITSIO_READINDX_HEAD[j] ) ;
    }

    j++ ;  NRD = RD_SNFITSIO_FLT(isn, "VPEC", &SNDATA.VPEC,
				 &SNFITSIO_READINDX_HEAD[j] ) ;

    j++ ;  NRD = RD_SNFITSIO_FLT(isn, "VPEC_ERR", &SNDATA.VPEC_ERR,
				 &SNFITSIO_READINDX_HEAD[j] ) ;

    // -------- HOST ---------

    j++ ;  NRD = RD_SNFITSIO_INT(isn, "HOSTGAL_NMATCH", 
				 &SNDATA.HOSTGAL_NMATCH[0],
				 &SNFITSIO_READINDX_HEAD[j] ) ;

    j++ ;  NRD = RD_SNFITSIO_INT(isn, "HOSTGAL_NMATCH2", 
				 &SNDATA.HOSTGAL_NMATCH[1],
				 &SNFITSIO_READINDX_HEAD[j] ) ;

    j++ ;  NRD = RD_SNFITSIO_FLT(isn, "HOSTGAL_CONFUSION", 
				 &SNDATA.HOSTGAL_CONFUSION,
				 &SNFITSIO_READINDX_HEAD[j] ) ;

    for(ifilt=0; ifilt < NFILT; ifilt++ ) {
      ifilt_obs = SNDATA_FILTER.MAP[ifilt];
      sprintf(KEY,"HOSTGAL_SB_FLUXCAL_%c", FILTERSTRING[ifilt_obs] );
      j++ ;  NRD = RD_SNFITSIO_FLT(isn, KEY, &SNDATA.HOSTGAL_SB_FLUXCAL[ifilt],
				   &SNFITSIO_READINDX_HEAD[j] ) ;
    }
    
    NGAL = MXHOSTGAL;
    for ( igal=0; igal < NGAL; igal++ ) {
      sprintf(PREFIX,"HOSTGAL");
      if ( igal > 0 ) { sprintf(PREFIX,"HOSTGAL%d", igal+1); }

      sprintf(KEY,"%s_OBJID", PREFIX);
      j++ ;  NRD = RD_SNFITSIO_DBL(isn, KEY, &D_OBJID,
				   &SNFITSIO_READINDX_HEAD[j] ) ;
      SNDATA.HOSTGAL_OBJID[igal] = (long long)D_OBJID;

       
      sprintf(KEY,"%s_PHOTOZ", PREFIX);
      j++ ;  NRD = RD_SNFITSIO_FLT(isn, KEY, &SNDATA.HOSTGAL_PHOTOZ[igal],
				 &SNFITSIO_READINDX_HEAD[j] ) ;

      sprintf(KEY,"%s_PHOTOZ_ERR", PREFIX);
      j++ ;  NRD = RD_SNFITSIO_FLT(isn, KEY, &SNDATA.HOSTGAL_PHOTOZ_ERR[igal],
				   &SNFITSIO_READINDX_HEAD[j] ) ;

      
      sprintf(KEY,"%s_SPECZ", PREFIX);
      j++ ;  NRD = RD_SNFITSIO_FLT(isn, KEY, &SNDATA.HOSTGAL_SPECZ[igal],
				   &SNFITSIO_READINDX_HEAD[j] ) ;
      sprintf(KEY,"%s_SPECZ_ERR", PREFIX);
      j++ ;  NRD = RD_SNFITSIO_FLT(isn, KEY, &SNDATA.HOSTGAL_SPECZ_ERR[igal],
				   &SNFITSIO_READINDX_HEAD[j] ) ;

      sprintf(KEY,"%s_RA", PREFIX);
      j++ ;  NRD = RD_SNFITSIO_DBL(isn, KEY, &SNDATA.HOSTGAL_RA[igal],
				   &SNFITSIO_READINDX_HEAD[j] ) ;

      sprintf(KEY,"%s_DEC", PREFIX);
      j++ ;  NRD = RD_SNFITSIO_DBL(isn, KEY, &SNDATA.HOSTGAL_DEC[igal],
				   &SNFITSIO_READINDX_HEAD[j] ) ;

      sprintf(KEY,"%s_SNSEP", PREFIX);
      j++ ;  NRD = RD_SNFITSIO_FLT(isn, KEY, &SNDATA.HOSTGAL_SNSEP[igal],
				   &SNFITSIO_READINDX_HEAD[j] ) ;

      sprintf(KEY,"%s_DDLR", PREFIX);
      j++ ;  NRD = RD_SNFITSIO_FLT(isn, KEY, &SNDATA.HOSTGAL_DDLR[igal],
				   &SNFITSIO_READINDX_HEAD[j] ) ;

      sprintf(KEY,"%s_LOGMASS", PREFIX);
      j++ ;  NRD = RD_SNFITSIO_FLT(isn, KEY, &SNDATA.HOSTGAL_LOGMASS_OBS[igal],
				   &SNFITSIO_READINDX_HEAD[j] ) ;

      sprintf(KEY,"%s_LOGMASS_ERR", PREFIX);
      j++ ;  NRD = RD_SNFITSIO_FLT(isn, KEY, &SNDATA.HOSTGAL_LOGMASS_ERR[igal],
				   &SNFITSIO_READINDX_HEAD[j] ) ;

      sprintf(KEY,"%s_sSFR", PREFIX);
      j++ ;  NRD = RD_SNFITSIO_FLT(isn, KEY, &SNDATA.HOSTGAL_sSFR[igal],
				   &SNFITSIO_READINDX_HEAD[j] ) ;

      sprintf(KEY,"%s_sSFR_ERR", PREFIX);
      j++ ;  NRD = RD_SNFITSIO_FLT(isn, KEY, &SNDATA.HOSTGAL_sSFR_ERR[igal],
				   &SNFITSIO_READINDX_HEAD[j] ) ;      

      
      for(ifilt=0; ifilt < NFILT; ifilt++ ) {
	ifilt_obs = SNDATA_FILTER.MAP[ifilt];
	sprintf(KEY,"%s_MAG_%c", PREFIX, FILTERSTRING[ifilt_obs] );
	j++ ;  NRD = RD_SNFITSIO_FLT(isn, KEY, &SNDATA.HOSTGAL_MAG[igal][ifilt],
				     &SNFITSIO_READINDX_HEAD[j] ) ;  
      }
      

    } // end igal 

    // - - - - -
    j++ ;  NRD = RD_SNFITSIO_FLT(isn, "PEAKMJD", &SNDATA.SEARCH_PEAKMJD,
				 &SNFITSIO_READINDX_HEAD[j] ) ;          

    j++ ;  NRD = RD_SNFITSIO_INT(isn, "SEARCH_TYPE", &SNDATA.SEARCH_TYPE,
				 &SNFITSIO_READINDX_HEAD[j] ) ;          

    // optional private var
    for ( ivar=1; ivar <= SNDATA.NVAR_PRIVATE; ivar++ ) {
      j++ ;  NRD = RD_SNFITSIO_DBL(isn, SNDATA.PRIVATE_KEYWORD[ivar],
				   &SNDATA.PRIVATE_VALUE[ivar],
				   &SNFITSIO_READINDX_HEAD[j] ) ;          
    }

    // ---------- SIM ----------
    if ( !SNFITSIO_SIMFLAG_SNANA ) { return(SUCCESS); }
    
    j++; NRD = RD_SNFITSIO_STR(isn, "SIM_MODEL_NAME", SNDATA.SIM_MODEL_NAME,
			       &SNFITSIO_READINDX_HEAD[j] ) ;

    j++; NRD = RD_SNFITSIO_INT(isn, "SIM_MODEL_INDEX", &SNDATA.SIM_MODEL_INDEX,
			       &SNFITSIO_READINDX_HEAD[j] ) ;

    j++; NRD = RD_SNFITSIO_STR(isn, "SIM_TYPE_NAME", SNDATA.SIM_TYPE_NAME,
			       &SNFITSIO_READINDX_HEAD[j] ) ;

    j++; NRD = RD_SNFITSIO_INT(isn, "SIM_TYPE_INDEX", &SNDATA.SIM_TYPE_INDEX,
			       &SNFITSIO_READINDX_HEAD[j] ) ;

    j++; NRD = RD_SNFITSIO_INT(isn, "SIM_SUBSAMPLE_INDEX", 
			       &SNDATA.SUBSAMPLE_INDEX,
			       &SNFITSIO_READINDX_HEAD[j] ) ;
   
    j++; NRD = RD_SNFITSIO_INT(isn, "SIM_TEMPLATE_INDEX", 
			       &SNDATA.SIM_TEMPLATE_INDEX,
			       &SNFITSIO_READINDX_HEAD[j] ) ;

    j++; NRD = RD_SNFITSIO_INT(isn, "SIM_LIBID", &SNDATA.SIM_LIBID,
			       &SNFITSIO_READINDX_HEAD[j] ) ;

    j++; NRD = RD_SNFITSIO_INT(isn, "SIM_NGEN_LIBID", &SNDATA.SIM_NGEN_LIBID,
			       &SNFITSIO_READINDX_HEAD[j] ) ;

    j++; NRD = RD_SNFITSIO_INT(isn, "SIM_NOBS_UNDEFINED", 
			       &SNDATA.SIM_NOBS_UNDEFINED,
			       &SNFITSIO_READINDX_HEAD[j] ) ;

    j++; NRD = RD_SNFITSIO_INT(isn, "SIM_SEARCHEFF_MASK", 
			       &SNDATA.SIM_SEARCHEFF_MASK,
			       &SNFITSIO_READINDX_HEAD[j] ) ;

    j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_REDSHIFT_HELIO", 
			       &SNDATA.SIM_REDSHIFT_HELIO,
			       &SNFITSIO_READINDX_HEAD[j] ) ;
    j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_REDSHIFT_CMB", 
			       &SNDATA.SIM_REDSHIFT_CMB,
			       &SNFITSIO_READINDX_HEAD[j] ) ;
    j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_REDSHIFT_HOST", 
			       &SNDATA.SIM_REDSHIFT_HOST,
			       &SNFITSIO_READINDX_HEAD[j] ) ;
    j++; NRD = RD_SNFITSIO_INT(isn, "SIM_REDSHIFT_FLAG", 
			       &SNDATA.SIM_REDSHIFT_FLAG,
			       &SNFITSIO_READINDX_HEAD[j] ) ;
    j++; NRD = RD_SNFITSIO_FLT(isn, "VPEC", &SNDATA.SIM_VPEC,
			       &SNFITSIO_READINDX_HEAD[j] ) ;

    j++; NRD = RD_SNFITSIO_DBL(isn, "SIM_HOSTLIB_GALID", &D_OBJID,
			       &SNFITSIO_READINDX_HEAD[j] ) ;
    SNDATA.SIM_HOSTLIB_GALID = (long long)D_OBJID ;

    j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_DLMU", &SNDATA.SIM_DLMU ,
			       &SNFITSIO_READINDX_HEAD[j] ) ;

    j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_LENSDMU", &SNDATA.SIM_LENSDMU ,
			       &SNFITSIO_READINDX_HEAD[j] ) ;

    j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_RA", &SNDATA.SIM_RA ,
			       &SNFITSIO_READINDX_HEAD[j] ) ;
    j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_DEC", &SNDATA.SIM_DEC ,
			       &SNFITSIO_READINDX_HEAD[j] ) ;

    j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_MWEBV", &SNDATA.SIM_MWEBV ,
			       &SNFITSIO_READINDX_HEAD[j] ) ;

    j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_PEAKMJD", &SNDATA.SIM_PEAKMJD ,
			       &SNFITSIO_READINDX_HEAD[j] ) ;

    j++; NRD = RD_SNFITSIO_FLT(isn,"SIM_MAGSMEAR_COH",&SNDATA.SIM_MAGSMEAR_COH,
			       &SNFITSIO_READINDX_HEAD[j] ) ;

    j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_AV", &SNDATA.SIM_AV ,
			       &SNFITSIO_READINDX_HEAD[j] ) ;
    j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_RV", &SNDATA.SIM_RV ,
			       &SNFITSIO_READINDX_HEAD[j] ) ;

    if ( SNDATA.SIM_MODEL_INDEX  == MODEL_SALT2 ) {
      j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_SALT2x0", &SNDATA.SIM_SALT2x0 ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;
      j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_SALT2x1", &SNDATA.SIM_SALT2x1 ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;
      j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_SALT2c", &SNDATA.SIM_SALT2c ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;
      j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_SALT2mB", &SNDATA.SIM_SALT2mB ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;
      j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_SALT2x0", &SNDATA.SIM_SALT2x0 ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;

      j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_SALT2alpha", &SNDATA.SIM_SALT2alpha ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;

      j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_SALT2beta", &SNDATA.SIM_SALT2beta ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;

      j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_SALT2gammaDM", 
				 &SNDATA.SIM_SALT2gammaDM ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;
    }
    if ( SNDATA.SIM_MODEL_INDEX  == MODEL_MLCS2k2 ) {
      j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_DELTA", &SNDATA.SIM_DELTA ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;
    }
    if ( SNDATA.SIM_MODEL_INDEX  == MODEL_SNOOPY ) {
      j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_STRETCH", &SNDATA.SIM_STRETCH ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;
    }
  
    if ( SNDATA.SIM_MODEL_INDEX == MODEL_SIMSED ) {
      for ( ipar=0; ipar < SNDATA.NPAR_SIMSED; ipar++ ) {
	j++; NRD = RD_SNFITSIO_FLT(isn, SNDATA.SIMSED_KEYWORD[ipar], 
				   &SNDATA.SIMSED_PARVAL[ipar] ,
				   &SNFITSIO_READINDX_HEAD[j] ) ;	
      }
    }

    if ( SNDATA.SIM_MODEL_INDEX == MODEL_BYOSED ||
	 SNDATA.SIM_MODEL_INDEX == MODEL_SNEMO ) {
      for ( ipar=0; ipar < SNDATA.NPAR_PySEDMODEL; ipar++ ) {
	j++; NRD = RD_SNFITSIO_FLT(isn, SNDATA.PySEDMODEL_KEYWORD[ipar], 
				   &SNDATA.PySEDMODEL_PARVAL[ipar] ,
				   &SNFITSIO_READINDX_HEAD[j] ) ;	
      }
    }

    if ( SNDATA.SIM_MODEL_INDEX == MODEL_LCLIB ) {
      for ( ipar=0; ipar < SNDATA.NPAR_LCLIB; ipar++ ) {
	sprintf(KEY, "%s", SNDATA.LCLIB_KEYWORD[ipar] );
	j++; NRD = RD_SNFITSIO_FLT(isn, KEY, &SNDATA.LCLIB_PARVAL[ipar],
				   &SNFITSIO_READINDX_HEAD[j] ) ;	
      }
      for ( ifilt=0; ifilt < SNDATA_FILTER.NDEF; ifilt++ ) {
	ifilt_obs  = SNDATA_FILTER.MAP[ifilt];
	sprintf(KEY, "SIM_TEMPLATEMAG_%c", FILTERSTRING[ifilt_obs] );
	j++; NRD = RD_SNFITSIO_FLT(isn, KEY, &SNDATA.SIM_PEAKMAG[ifilt] ,
				   &SNFITSIO_READINDX_HEAD[j] ) ; 
      }	
    } // end MODEL_LCLIB

    // filter-dependent stuff
    for ( ifilt=0; ifilt < SNDATA_FILTER.NDEF; ifilt++ ) {
      ifilt_obs  = SNDATA_FILTER.MAP[ifilt];

      sprintf(KEY, "SIM_PEAKMAG_%c", FILTERSTRING[ifilt_obs] );
      j++; NRD = RD_SNFITSIO_FLT(isn, KEY, &SNDATA.SIM_PEAKMAG[ifilt] ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;	
      sprintf(KEY, "SIM_EXPOSURE_%c", FILTERSTRING[ifilt_obs] );
      j++; NRD = RD_SNFITSIO_FLT(isn, KEY, &SNDATA.SIM_EXPOSURE_TIME[ifilt] ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;	
      sprintf(KEY, "SIM_GALFRAC_%c", FILTERSTRING[ifilt_obs] );
      j++; NRD = RD_SNFITSIO_FLT(isn, KEY, &SNDATA.SIM_GALFRAC[ifilt] ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;	
    }

    if ( SNDATA.SIM_SL_FLAG ) { // bug? not sure if this is set in readback
      char PREFIX_SL[] = "SIM_STRONGLENS" ;

      sprintf(KEY, "%s_ID", PREFIX_SL);
      j++; NRD = RD_SNFITSIO_INT(isn, KEY, &SNDATA.SIM_SL_IDLENS ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;
      sprintf(KEY, "%s_z", PREFIX_SL);
      j++; NRD = RD_SNFITSIO_DBL(isn, KEY, &SNDATA.SIM_SL_zLENS ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;
      sprintf(KEY, "%s_TDELAY", PREFIX_SL);
      j++; NRD = RD_SNFITSIO_DBL(isn, KEY, &SNDATA.SIM_SL_TDELAY ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;
      sprintf(KEY, "%s_MAGSHIFT", PREFIX_SL);
      j++; NRD = RD_SNFITSIO_DBL(isn, KEY, &SNDATA.SIM_SL_MAGSHIFT ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;
      sprintf(KEY, "%s_NIMG", PREFIX_SL);
      j++; NRD = RD_SNFITSIO_INT(isn, KEY, &SNDATA.SIM_SL_NIMG ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;
      sprintf(KEY, "%s_IMGNUM", PREFIX_SL);
      j++; NRD = RD_SNFITSIO_INT(isn, KEY, &SNDATA.SIM_SL_IMGNUM ,
				 &SNFITSIO_READINDX_HEAD[j] ) ;
    }


  } // end LRD_HEAD

  // - - - - - - - -

  if ( LRD_PHOT ) {
    int ep, ep0 = 1 ;
    int NSPLIT ;
    j=0;

    j++; NRD = RD_SNFITSIO_DBL(isn, "MJD", &SNDATA.MJD[ep0], 
			       &SNFITSIO_READINDX_PHOT[j] ) ;

    // note that FLT returns comma-separated list in 1D string
    // Allow either FLT or BAND column name
    j++; NRD = RD_SNFITSIO_STR(isn, "FLT", SNDATA.FILTCHAR_1D, 
			       &SNFITSIO_READINDX_PHOT[j] ) ;
    if ( NRD == 0 ) {
      j++; NRD = RD_SNFITSIO_STR(isn, "BAND", SNDATA.FILTCHAR_1D, 
				 &SNFITSIO_READINDX_PHOT[j] ) ;
    }

    // store arrays need to re-write in text format
    for(ep=0; ep<=NRD; ep++) { SNDATA.OBSFLAG_WRITE[ep] = true ; }

    j++; NRD = RD_SNFITSIO_INT(isn, "CCDNUM", &SNDATA.CCDNUM[ep0], 
				 &SNFITSIO_READINDX_PHOT[j] ) ;

    // note that FIELD returns comma-separated list in 1D string
    j++; NRD = RD_SNFITSIO_STR(isn, "FIELD", SNDATA.FIELDNAME_1D, 
			       &SNFITSIO_READINDX_PHOT[j] ) ;

    j++; NRD = RD_SNFITSIO_INT(isn, "PHOTFLAG", &SNDATA.PHOTFLAG[ep0], 
				 &SNFITSIO_READINDX_PHOT[j] ) ;
    j++; NRD = RD_SNFITSIO_FLT(isn, "PHOTPROB", &SNDATA.PHOTPROB[ep0], 
				 &SNFITSIO_READINDX_PHOT[j] ) ;

    j++; NRD = RD_SNFITSIO_FLT(isn, "FLUXCAL", &SNDATA.FLUXCAL[ep0], 
				 &SNFITSIO_READINDX_PHOT[j] ) ;
    j++; NRD = RD_SNFITSIO_FLT(isn,"FLUXCALERR",&SNDATA.FLUXCAL_ERRTOT[ep0], 
				 &SNFITSIO_READINDX_PHOT[j] ) ;

    j++; NRD = RD_SNFITSIO_FLT(isn, "PSF_SIG1", &SNDATA.PSF_SIG1[ep0], 
			       &SNFITSIO_READINDX_PHOT[j] ) ;
    j++; NRD = RD_SNFITSIO_FLT(isn, "PSF_SIG2", &SNDATA.PSF_SIG2[ep0], 
			       &SNFITSIO_READINDX_PHOT[j] ) ;
    j++; NRD = RD_SNFITSIO_FLT(isn, "PSF_RATIO", &SNDATA.PSF_RATIO[ep0], 
			       &SNFITSIO_READINDX_PHOT[j] ) ;

    j++; NRD = RD_SNFITSIO_FLT(isn, "PSF_NEA", &SNDATA.PSF_NEA[ep0], 
			       &SNFITSIO_READINDX_PHOT[j] ) ;

    j++; NRD = RD_SNFITSIO_FLT(isn, "SKY_SIG", &SNDATA.SKY_SIG[ep0], 
			       &SNFITSIO_READINDX_PHOT[j] ) ;
    j++; NRD = RD_SNFITSIO_FLT(isn, "SKY_SIG_T", &SNDATA.SKY_SIG_T[ep0], 
			       &SNFITSIO_READINDX_PHOT[j] ) ;
    j++; NRD = RD_SNFITSIO_FLT(isn, "ZEROPT", &SNDATA.ZEROPT[ep0], 
			       &SNFITSIO_READINDX_PHOT[j] ) ;
    j++; NRD = RD_SNFITSIO_FLT(isn, "ZEROPT_ERR", &SNDATA.ZEROPT_ERR[ep0], 
			       &SNFITSIO_READINDX_PHOT[j] ) ;
    j++; NRD = RD_SNFITSIO_FLT(isn, "GAIN", &SNDATA.GAIN[ep0], 
			       &SNFITSIO_READINDX_PHOT[j] ) ;

    if ( SNDATA.NXPIX > 0 ) {
      j++; NRD = RD_SNFITSIO_FLT(isn, "XPIX", &SNDATA.XPIX[ep0], 
				 &SNFITSIO_READINDX_PHOT[j] ) ;
      j++; NRD = RD_SNFITSIO_FLT(isn, "YPIX", &SNDATA.YPIX[ep0], 
				 &SNFITSIO_READINDX_PHOT[j] ) ;
    }

    if ( SNFITSIO_SIMFLAG_SNANA || SNFITSIO_SIMFLAG_MAGOBS )  {
      j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_MAGOBS", &SNDATA.SIMEPOCH_MAG[ep0], 
				 &SNFITSIO_READINDX_PHOT[j] ) ;
    }

    if ( SNFITSIO_SIMFLAG_SNANA ) {
      j++; NRD = RD_SNFITSIO_FLT(isn, "SIM_FLUXCAL_HOSTERR", 
				 &SNDATA.SIMEPOCH_FLUXCAL_HOSTERR[ep0], 
				 &SNFITSIO_READINDX_PHOT[j] ) ;
    }
    if ( SNFITSIO_SIMFLAG_SNRMON ) {
      j++; NRD = RD_SNFITSIO_FLT(isn, SNDATA.VARNAME_SNRMON,
				 &SNDATA.SIMEPOCH_SNRMON[ep0], 
				 &SNFITSIO_READINDX_PHOT[j] ) ;
    }

  } // end LRD_PHOT

  // - - - - - - - - - - -

  if ( LRD_SPEC &&  SNFITSIO_SIMFLAG_SPECTROGRAPH ) {
    // load GENSPEC struct in sntools_spectrograph.h (Feb 19 2021)
    int NSPEC, irow, ROWMIN, ROWMAX, NBLAM, ispec=0;

    NSPEC = RD_SNFITSIO_SPECROWS(SNDATA.CCID, &ROWMIN, &ROWMAX) ;
    GENSPEC.NMJD_PROC = 0 ;
    if ( NSPEC <= 0 ) { return(SUCCESS); }

    for(irow = ROWMIN; irow <= ROWMAX; irow++ ) {

      NBLAM = RDSPEC_SNFITSIO_HEADER.NLAMBIN[irow] ;
      init_GENSPEC_EVENT(ispec,NBLAM);   // malloc GENSPEC

      GENSPEC.NMJD_PROC++ ;  GENSPEC.NMJD_TOT = GENSPEC.NMJD_PROC;
      GENSPEC.NBLAM_TOT            = NBLAM ;  
      GENSPEC.NBLAM_VALID[ispec]   = NBLAM ;
      GENSPEC.MJD_LIST[ispec]      = RDSPEC_SNFITSIO_HEADER.MJD[irow];
      GENSPEC.TEXPOSE_LIST[ispec]  = RDSPEC_SNFITSIO_HEADER.TEXPOSE[irow];
      GENSPEC.ID_LIST[ispec]       = ispec+1 ;  // ID starts at 1

      RD_SNFITSIO_SPECDATA(irow  
			   ,GENSPEC.LAMMIN_LIST[ispec] 
			   ,GENSPEC.LAMMAX_LIST[ispec] 
			   ,GENSPEC.FLAM_LIST[ispec] 
			   ,GENSPEC.FLAMERR_LIST[ispec] 
			   ,GENSPEC.GENFLAM_LIST[ispec]   ) ;
      ispec++ ;
    } // end irow loop over rows

  } // end LRD_SPEC

  return(SUCCESS);

} // end RD_SNFITSIO_EVENT

int rd_snfitsio_event__(int *OPT, int *isn )
{ return RD_SNFITSIO_EVENT(*OPT,*isn); }


// ===============================================
void RD_SNFITSIO_CLOSE(char *version) {

  char fnam[] = "RD_SNFITSIO_CLOSE" ;

  // ------------- BEGIN --------------

  if ( strcmp(version,SNFITSIO_PHOT_VERSION) != 0 ) {
    sprintf(c1err,"Cannot close fits-files for version %s", version);
    sprintf(c2err,"because current fits version is %s", 
	    SNFITSIO_PHOT_VERSION);
    errmsg(SEV_FATAL, 0, fnam, c1err, c2err); 
  }

  snfitsio_close(IFILE_SNFITSIO, ITYPE_SNFITSIO_HEAD );
  snfitsio_close(IFILE_SNFITSIO, ITYPE_SNFITSIO_PHOT );   

  if ( SNFITSIO_SIMFLAG_SPECTROGRAPH )
    { snfitsio_close(IFILE_SNFITSIO, ITYPE_SNFITSIO_SPEC );}

  // free memory
  rd_snfitsio_free(IFILE_SNFITSIO, ITYPE_SNFITSIO_HEAD );
  rd_snfitsio_free(IFILE_SNFITSIO, ITYPE_SNFITSIO_PHOT );
  if ( SNFITSIO_SIMFLAG_SPECTROGRAPH ) { ; } // nothing to free

} // end of RD_SNFITSIO_CLOSE

void  rd_snfitsio_close__(char *version) {
  RD_SNFITSIO_CLOSE(version);
}

// ===========================================================
void  GET_SNFITSIO_INFO(char *VERSION, char *FILENAME_HEAD,
                        char *FILENAME_PHOT, int *IFILE ) {

  // Created Aug 16 2018
  // Return current info about files being read.
  // ------------- BEGIN ------------------

  sprintf(VERSION,"%s", SNFITSIO_PHOT_VERSION );
  *IFILE = IFILE_SNFITSIO ;

  sprintf(FILENAME_HEAD, "%s",
	  snfitsFile[IFILE_SNFITSIO][ITYPE_SNFITSIO_HEAD] );

  sprintf(FILENAME_PHOT, "%s",
	  snfitsFile[IFILE_SNFITSIO][ITYPE_SNFITSIO_PHOT] );

  return ;

} // end GET_SNFITSIO_INFO

void get_snfitsio_info__(char *VERSION, char *FILENAME_HEAD,
			 char *FILENAME_PHOT, int *IFILE ){
  GET_SNFITSIO_INFO(VERSION,FILENAME_HEAD, FILENAME_PHOT, IFILE);
}

// ===============================================
int rd_snfitsio_list(void) {

  // read ascii LIST file for list of FITS-header files.
  // If the first file does not have a .FITS suffix
  // then return ERROR, meaning that the data are 
  // in ascii format. A mix of ASCII and FITS data files
  // is NOT allowed.
  // Return SUCCESS if these are fits files.
  //
  // 4/30/2014: abort if NFILE_SNFITSIO exceeds bound.
  //

  FILE *fp ;
  int  N, itype ;
  char *ptrTmp, tmpFile[MXPATHLEN];
  char fnam[] = "rd_snfitsio_list" ;

  // ----------- BEGIN ---------

  if ( (fp = fopen(SNFITSIO_LISTFILE, "rt"))==NULL ) {       
    sprintf ( c1err, "Cannot open LIST file :" );
    sprintf ( c2err," '%s' ", SNFITSIO_LISTFILE );
    errmsg(SEV_FATAL, 0, fnam, c1err, c2err); 
  }

  itype = ITYPE_SNFITSIO_HEAD ;
  NFILE_SNFITSIO = 0;
  while( (fscanf(fp, "%s", tmpFile)) != EOF) {
    NFILE_SNFITSIO++ ;

    if ( NFILE_SNFITSIO >= MXFILE_SNFITSIO ) { continue ; }
    N = NFILE_SNFITSIO ;

    ptrTmp = snfitsFile[N][itype] ;
    sprintf(ptrTmp, "%s", tmpFile );

    if ( is_fits(ptrTmp) == 0 ) { return ERROR; }

    // create full name including path
    ptrTmp = snfitsFile_plusPath[N][itype] ;
    sprintf(ptrTmp, "%s/%s", SNFITSIO_DATA_PATH, tmpFile );
  }

  fclose(fp);  

  if ( NFILE_SNFITSIO >= MXFILE_SNFITSIO ) {
    sprintf(c1err,"NFILE_SNFITSIO = %d exceeds bound of MXFILE_SNFITSIO=%d", 
	    NFILE_SNFITSIO, MXFILE_SNFITSIO );
    sprintf(c2err,"Check %s", SNFITSIO_LISTFILE);
    errmsg(SEV_FATAL, 0, fnam, c1err, c2err); 
  }
  else if ( NFILE_SNFITSIO > 0 ) 
    { return SUCCESS ; }
  else {
    sprintf(c1err,"Found no files in");
    sprintf(c2err,"%s", SNFITSIO_LISTFILE);
    errmsg(SEV_FATAL, 0, fnam, c1err, c2err); 
  }

  return ERROR ;

} // end of rd_snfitsio_list


// =======================
int is_fits(char *file) {

  // Dec 2012: return 1 if extention is .FITS or .fits; 0 otherwise.
  // Note that this logic is fragile.

  char *SUFFIX, *suffix ;

  // check if this is a fits file based on the extentions
  SUFFIX = strstr(file,".FITS");
  suffix = strstr(file,".fits");

  if ( suffix == NULL && SUFFIX == NULL ) 
    { return 0 ; }
  else
    { return 1; }
}

// ================================
void rd_snfitsio_open(int ifile, int photflag_open, int vbose) {

  // Open snfits files (HEAD, PHOT, and optional SPEC) for reading.
  // First open the HEADER file.
  // Next read the name of the PHOT file from the HEAD file.
  // Next read name of optional SPEC file from HEAD file.
  // Next open the PHOT file IF photflag_open=1.
  // 
  // Finally, set NSNLC_SNFITSIO[ifile] = NROW ; 
  //
  // Note that fitsFile pointers fp_snfitsFile[itype]
  // are both opened for reading.
  //
  // Dec 27, 2015: read SIMLIB_MSKOPT
  // Feb 07, 2018: pass photflag_open arg so that only header can be opened.
  // Apr 15, 2019: check for optional SPEC file
  // Oct 26, 2020: read SNANA_VERSION in fits header

  fitsfile *fp ;
  int istat, itype, istat_spec, hdutype, nrow, nmove = 1  ;
  char keyname[60], comment[200], *ptrFile ;
  char fnam[] = "rd_snfitsio_open" ;

  // ------------- BEGIN -------------

  init_SNDATA_EVENT();

  istat = 0;
  itype   = ITYPE_SNFITSIO_HEAD ;
  ptrFile = snfitsFile_plusPath[ifile][itype];
  fits_open_file(&fp_snfitsFile[itype], ptrFile, READONLY, &istat );
  sprintf(c1err,"Open %s", snfitsFile[ifile][itype] );
  snfitsio_errorCheck(c1err, istat);

  if ( vbose ) { printf("   Open %s  \n",  snfitsFile[ifile][itype] ); }

  fp = fp_snfitsFile[itype] ;

  istat = 0;

  // read internal code version starting Feb 11 2014.
  // Allow reading older files that do not have this key.
  sprintf(keyname, "%s", "CODE_IVERSION" );  
  fits_read_key(fp,TINT,keyname,&SNFITSIO_CODE_IVERSION,comment,&istat);
  if ( istat ) { SNFITSIO_CODE_IVERSION = 1; } // no key -> default 
  istat = 0;  // reset in case CODE_IVERSION key does not exist.

  // - - - - - - - -
  // Oct 2020: read SNANA_VERSION in header
  // Jan 12 2021: if no SNANA_VERSION key, replace v10_30 with UNKNOWN
  sprintf(keyname, "%s", "SNANA_VERSION" );  
  fits_read_key(fp, TSTRING, keyname,
		&SNDATA.SNANA_VERSION, comment, &istat);
  if ( istat ) { sprintf(SNDATA.SNANA_VERSION,"UNKNOWN"); } 
  istat = 0;  // reset in case SNANA_VERSION key does not exist.

  // - - - - - - - - - - -
  // read name of survey from HEADER file
  sprintf(keyname, "%s", "SURVEY" );  
  fits_read_key(fp, TSTRING, keyname, 
		&SNDATA.SURVEY_NAME, comment, &istat );
  sprintf(c1err, "read %s key", keyname);
  snfitsio_errorCheck(c1err, istat); 

  // read sub-survey flag if it's there. 
  // For back-compatibility, allow missing key for older sims.
  sprintf(keyname, "%s", "SUBSURVEY_FLAG" );  
  fits_read_key(fp, TINT, keyname, 
		&SNDATA.SUBSURVEY_FLAG, comment, &istat );
  if ( istat != 0 ) { SNDATA.SUBSURVEY_FLAG = 0 ; }
  istat = 0 ;

  // read list of filters
  char filter_list[MXFILTINDX];
  sprintf(keyname, "%s", "FILTERS" );
  fits_read_key(fp, TSTRING, keyname, 
		&filter_list, comment, &istat );
  sprintf(c1err, "read %s key", keyname);
  snfitsio_errorCheck(c1err, istat); 

  // restore SNDATA_FILTER struct
  set_SNDATA_FILTER(filter_list);  // Feb 15 2021


  // read data type
  sprintf(keyname, "%s", "DATATYPE" );
  fits_read_key(fp, TSTRING, keyname, 
		&SNDATA.DATATYPE, comment, &istat );
  sprintf(c1err, "read %s key", keyname);
  snfitsio_errorCheck(c1err, istat); 

  char *DTYPE = SNDATA.DATATYPE ;
  SNFITSIO_DATAFLAG       = (strcmp(DTYPE, DATATYPE_DATA      ) == 0 );
  SNFITSIO_SIMFLAG_SNANA  = (strcmp(DTYPE, DATATYPE_SIM_SNANA ) == 0 );
  SNFITSIO_SIMFLAG_MAGOBS = (strcmp(DTYPE, DATATYPE_SIM_MAGOBS) == 0 );

  // read name of PHOTOMETRY file from HEADER file
  itype = ITYPE_SNFITSIO_PHOT ;  
  sprintf(keyname, "%s", "PHOTFILE" );
  fits_read_key(fp, TSTRING, keyname, 
		&snfitsFile[ifile][itype], comment, &istat );
  sprintf(c1err, "read %s key", keyname);
  snfitsio_errorCheck(c1err, istat); 

  // construct full name of PHOT file.
  sprintf(snfitsFile_plusPath[ifile][itype],"%s/%s", 
	  SNFITSIO_DATA_PATH, snfitsFile[ifile][itype] );


  // read name of optional SPEC file from HEADER file (Apri 2019)
  itype = ITYPE_SNFITSIO_SPEC ;  istat_spec=0;
  sprintf(keyname, "%s", "SPECFILE" );
  fits_read_key(fp, TSTRING, keyname,
		&snfitsFile[ifile][itype], comment, &istat_spec );
  if ( istat_spec == 0 ) {
    SNFITSIO_SIMFLAG_SPECTROGRAPH = true ;
    sprintf(snfitsFile_plusPath[ifile][itype],"%s/%s", 
	    SNFITSIO_DATA_PATH, snfitsFile[ifile][itype] );
  }
  else {
    SNFITSIO_SIMFLAG_SPECTROGRAPH = false ;
    sprintf(snfitsFile[ifile][itype],"NONE");
  }

  // check optional PRIVATE header keys.
  rd_snfitsio_private();



  if ( SNFITSIO_SIMFLAG_SNANA ) {

    // read name of SIMLIB_FILE file
    // Legacy sims use "SIMLIB", so don't check for error.
    istat = 0 ;
    sprintf(keyname, "%s", "SIMLIB_FILE" );
    fits_read_key(fp, TSTRING, keyname, 
		  &SNDATA.SIMLIB_FILE, comment, &istat );
    sprintf(c1err, "read %s key", keyname);
    //  snfitsio_errorCheck(c1err, istat);    

    // Dec 27 2015 read new key
    istat = 0 ;
    sprintf(keyname, "%s", "SIMLIB_MSKOPT" );
    fits_read_key(fp, TINT, keyname, 
		  &SNDATA.SIMLIB_MSKOPT, comment, &istat );
    sprintf(c1err, "read %s key", keyname);
    //  snfitsio_errorCheck(c1err, istat);    


    // read global info for Galactic extinction
    istat = 0 ;
    sprintf(keyname, "%s", "SIMOPT_MWCOLORLAW" );
    fits_read_key(fp, TINT, keyname, 
		  &SNDATA.SIMOPT_MWCOLORLAW, comment, &istat );
    sprintf(c1err, "read %s key", keyname);
    snfitsio_errorCheck(c1err, istat);    

    istat = 0 ;
    sprintf(keyname, "%s", "SIM_MWRV" );
    fits_read_key(fp, TFLOAT, keyname, 
		  &SNDATA.SIM_MWRV, comment, &istat );
    sprintf(c1err, "read %s key", keyname);
    snfitsio_errorCheck(c1err, istat);    

    istat = 0 ;
    sprintf(keyname, "%s", "SIMOPT_MWEBV" );
    fits_read_key(fp, TINT, keyname, 
		  &SNDATA.SIMOPT_MWEBV, comment, &istat );
    sprintf(c1err, "read %s key", keyname);
    snfitsio_errorCheck(c1err, istat);    


    // read optional flag to fudge flux-errors ;
    // it is optional for back-compatibility
    istat = 0 ;
    sprintf(keyname, "%s", "SIMOPT_FLUXERR" );
    fits_read_key(fp, TINT, keyname, 
		  &SNDATA.SIMOPT_FLUXERR, comment, &istat );

    // check optional SIM header keys.
    rd_snfitsio_simkeys();

  }

  // ---------------------------
  // Now open the PHOT file.  
  int NFILE_OPEN = 1;
  if ( photflag_open ) {
    NFILE_OPEN++ ;
    itype   = ITYPE_SNFITSIO_PHOT ;
    istat   = 0;
    ptrFile = snfitsFile_plusPath[ifile][itype];
    fits_open_file(&fp_snfitsFile[itype], ptrFile, READONLY, &istat );
    sprintf(c1err,"Open %s", snfitsFile[ifile][itype] );
    snfitsio_errorCheck(c1err, istat);
    if ( vbose ) { printf("   Open %s \n", snfitsFile[ifile][itype] ); }
  }

  // move to table in each file
  for ( itype = 0; itype < NFILE_OPEN ; itype++ ) {
    istat   = 0 ;
    fp      = fp_snfitsFile[itype] ;
    fits_movrel_hdu( fp, nmove, &hdutype, &istat );
    sprintf(c1err,"movrel to %s table", snfitsType[itype] ) ;
    snfitsio_errorCheck(c1err, istat);
  }

  // read Number of rows (NAXIS2) in header file
  // to know how many SN are stored here.

  long NROW ;
  istat = 0 ;
  itype = ITYPE_SNFITSIO_HEAD ;
  fp    = fp_snfitsFile[itype] ;
  sprintf(keyname, "%s", "NAXIS2" );
  fits_read_key(fp, TLONG, keyname,  &NROW, comment, &istat );
  sprintf(c1err, "read %s key", keyname);
  snfitsio_errorCheck(c1err, istat); 
  
  if ( vbose ) {  nrow = (int)NROW;
    printf("   SURVEY=%s    FILTERS=%s   N(SNe)=%d  \n", 
	   SNDATA.SURVEY_NAME, SNDATA_FILTER.LIST, nrow  );
    fflush(stdout);
  }

  NSNLC_SNFITSIO[ifile] = NROW ; // store globally

  return ;

} // end of rd_snfitsio_open


// ==========================
void rd_snfitsio_simkeys(void) {

  // check option sim keys for  SIMSED, LCLIB, SIM_HOSTLIB
  // Mar 18 2018: read SIM_VARNAME_SNRMON
  // Dec 26 2018: check SNFITSIO_CODE_IVERSION for reading SIMSED params.
  // Feb 10 2021: check SIM_SL_FLAG

  fitsfile *fp ;
  int itype, istat, NPAR, ipar ;
  char  keyname[60], comment[200], *cptr    ;
  char  fnam[] = "rd_snfitsio_simkeys"  ;

  // ------------ BEGIN ------------

  SNDATA.NPAR_SIMSED      = 0;
  SNDATA.NPAR_PySEDMODEL  = 0;
  SNDATA.NPAR_LCLIB       = 0;
  SNDATA.NPAR_SIM_HOSTLIB = 0 ;

  itype   = ITYPE_SNFITSIO_HEAD ;
  fp      = fp_snfitsFile[itype] ;

  // check SIMSED_NPAR 
  istat = 0;
  sprintf(keyname, "%s", "SIMSED_NPAR" );
  fits_read_key(fp, TINT, keyname, &NPAR, comment, &istat );
  if ( istat == 0  && NPAR > 0 ) {
    SNDATA.NPAR_SIMSED = NPAR ;  
    int IPAR_START=0, IPAR_END=NPAR-1 ;    
    if ( SNFITSIO_CODE_IVERSION < 8 ) 
      { IPAR_START=1; IPAR_END=NPAR ; }  // legacy 
    
    for ( ipar=IPAR_START; ipar <= IPAR_END; ipar++ ) {
      sprintf(keyname,"SIMSED_PAR%2.2d", ipar);
      cptr = SNDATA.SIMSED_KEYWORD[ipar];
      fits_read_key(fp, TSTRING, keyname, cptr, comment, &istat );
    }
  }


  // - - - - - - - -
  // Check PySEDMODELs 
  int NCHOICE=2, imodel; char tmpModel[40];
  char PySEDMODEL_CHOICE_LIST[2][20] = { "BYOSED", "SNEMO" } ;
  
  for(imodel = 0; imodel < NCHOICE; imodel++ ) {
    sprintf(tmpModel, "%s", PySEDMODEL_CHOICE_LIST[imodel] );
    istat = NPAR = 0;
    sprintf(keyname, "%s_NPAR", tmpModel ); 
    fits_read_key(fp, TINT, keyname, &NPAR, comment, &istat );
    if ( istat == 0  && NPAR > 0 ) {
      sprintf(SNDATA.PySEDMODEL_NAME, "%s", tmpModel);
      SNDATA.NPAR_PySEDMODEL = NPAR ;  
      for ( ipar=0; ipar < NPAR; ipar++ ) {
	sprintf(keyname,"%s_PAR%2.2d", tmpModel, ipar); 
	cptr = SNDATA.PySEDMODEL_KEYWORD[ipar];
	fits_read_key(fp, TSTRING, keyname, cptr, comment, &istat );
      }
    }
  } // end imodel loop

  // check LCLIB_NPAR 
  istat = 0;
  sprintf(keyname, "%s", "LCLIB_NPAR" );
  fits_read_key(fp, TINT, keyname, &NPAR, comment, &istat );
  if ( istat == 0  && NPAR > 0 ) {
    SNDATA.NPAR_LCLIB = NPAR ;  
    for ( ipar=0; ipar < NPAR; ipar++ ) {
      sprintf(keyname,"LCLIB_PAR%2.2d", ipar);
      cptr = SNDATA.LCLIB_KEYWORD[ipar];
      fits_read_key(fp, TSTRING, keyname, cptr, comment, &istat );
    }
  }


  // check SIM_HOSTLIB_NPAR
  istat = 0;
  sprintf(keyname, "%s", "SIM_HOSTLIB_NPAR" );
  fits_read_key(fp, TINT, keyname, &NPAR, comment, &istat );
  if ( istat == 0  && NPAR > 0 ) {
    SNDATA.NPAR_SIM_HOSTLIB = NPAR ;  
    for ( ipar=0; ipar < NPAR; ipar++ ) {
      sprintf(keyname,"SIM_HOSTLIB_PAR%2.2d", ipar);
      cptr = SNDATA.SIM_HOSTLIB_KEYWORD[ipar];
      fits_read_key(fp, TSTRING, keyname, cptr, comment, &istat );
    }
  }

  // check  SIM_VARNAME_SNRMON
  istat = 0 ;
  sprintf(keyname,"SIM_VARNAME_SNRMON");
  fits_read_key(fp, TSTRING, keyname, SNDATA.VARNAME_SNRMON, comment, &istat );

  // check  SIM_SL_FLAG (Feb 2021)
  istat = 0 ;
  sprintf(keyname,"SIM_SL_FLAG");
  fits_read_key(fp, TINT, keyname, &SNDATA.SIM_SL_FLAG, comment, &istat );

  return ;

} // end of  rd_snfitsio_simkeys


// ==========================
void rd_snfitsio_private(void) {

  fitsfile *fp ;
  int itype, istat, NVAR, ivar ;
  char keyname[60], comment[200], *cptr ;
  //  char fnam[] = "rd_snfitsio_private" ;

  // ------------ BEGIN ------------

  SNDATA.NVAR_PRIVATE = 0;

  itype   = ITYPE_SNFITSIO_HEAD ;
  fp      = fp_snfitsFile[itype] ;

  // if NPRIVATE key does not exist, just leave NVAR_PRIVATE=0 and return
  istat = 0;
  sprintf(keyname, "%s", "NPRIVATE" );
  fits_read_key(fp, TINT, keyname, &NVAR, comment, &istat );
  if ( istat != 0 ) { return ; }
  SNDATA.NVAR_PRIVATE = NVAR ;
  
  /*
  printf(" xxx fits_read:  istat = %d and NVAR_PRIVATE = %d \n",
	 istat, NVAR);
  */

  if ( NVAR > 0 ) {
    for ( ivar=1; ivar <= NVAR; ivar++ ) {
      sprintf(keyname,"PRIVATE%d", ivar);
      cptr = SNDATA.PRIVATE_KEYWORD[ivar];
      fits_read_key(fp, TSTRING, keyname, cptr, comment, &istat );
    }
  }

} // end of  rd_snfitsio_private


// =================================================
void rd_snfitsio_file(int ifile) {

  int photflag_open=1;
  int vbose=0;
  //  char fnam[] = "rd_snfitsio_file" ;

  // ----------- BEGIN --------------

  MXOBS_SNFITSIO     =  0 ;

  // open header fits-file and the phot fits-file
  rd_snfitsio_open(ifile, photflag_open, vbose);    

  // read table parNames and forms
  rd_snfitsio_tblpar( ifile, ITYPE_SNFITSIO_HEAD );  
  rd_snfitsio_tblpar( ifile, ITYPE_SNFITSIO_PHOT );

  // allocate memory for header 
  rd_snfitsio_malloc( ifile, ITYPE_SNFITSIO_HEAD, NSNLC_SNFITSIO[ifile] );

  // read/store header info for each SN
  rd_snfitsio_head(ifile);

  // allocate lightcurve [PHOT] memory after reading header.
  rd_snfitsio_malloc( ifile, ITYPE_SNFITSIO_PHOT, MXOBS_SNFITSIO );  

  
} // end of rd_snfitsio_file


// =================================================
void rd_snfitsio_tblpar(int ifile, int itype) {

  long NCOLUMN ;
  int  istat, icol, iform, npar, ncol  ;
  int  LDMP = 1 ;
  fitsfile *fp ;

  char  keyname[100], comment[200], *ptrTmp  ;
  //  char  fnam[] = "rd_snfitsio_tblpar"    ;

  // ------------ BEGIN --------------

  istat = 0 ;
  fp    = fp_snfitsFile[itype] ;
  sprintf(keyname, "%s", "TFIELDS" );
  fits_read_key(fp, TLONG, keyname,  &NCOLUMN, comment, &istat );
  sprintf(c1err, "read %s key", keyname);
  snfitsio_errorCheck(c1err, istat); 
  NPAR_SNFITSIO[itype] = (int)NCOLUMN ;
  
  if ( LDMP ) { 
    ncol = (int)NCOLUMN ;
    printf("   %s contains %d columns. \n", 
	   snfitsFile[ifile][itype], ncol );
  }

  for ( iform=0; iform < MXFORM_SNFITSIO; iform++ ) 
    { RD_SNFITSIO_TABLEVAL[itype].NPAR[iform] = 0 ; }

  // loop over colum TTYPE[1-NFIELD] and count how many
  // float, double, int ...  Also store column name

  for ( icol = 1; icol <= NCOLUMN; icol++ ) {

    istat = 0 ;
    sprintf(keyname,"TTYPE%d", icol );
    ptrTmp = SNFITSIO_TABLEDEF[itype].name[icol];
    fits_read_key(fp, TSTRING, keyname,  ptrTmp, comment, &istat );
    sprintf(c1err, "read %s key", keyname);
    snfitsio_errorCheck(c1err, istat);

    istat = 0 ;
    sprintf(keyname,"TFORM%d", icol );
    ptrTmp = SNFITSIO_TABLEDEF[itype].form[icol];
    fits_read_key(fp, TSTRING, keyname,  ptrTmp, comment, &istat );
    sprintf(c1err, "read %s key", keyname);
    snfitsio_errorCheck(c1err, istat);

    // keep track of how many header parameters are of each form
    iform = formIndex_snfitsio(ptrTmp);
    SNFITSIO_TABLEDEF[itype].iform[icol] = iform ;

    RD_SNFITSIO_TABLEVAL[itype].NPAR[iform]++ ;
    npar = RD_SNFITSIO_TABLEVAL[itype].NPAR[iform] ;
    RD_SNFITSIO_TABLEVAL[itype].IPAR[iform][npar]    = icol ;
    RD_SNFITSIO_TABLEVAL[itype].IPARINV[iform][icol] = npar ;

    if ( LDMP ) {
      printf("\t  Found %s-Param[%2d] = %s (form = %s)\n"
	     ,snfitsType[itype], icol
	     ,SNFITSIO_TABLEDEF[itype].name[icol]
	     ,SNFITSIO_TABLEDEF[itype].form[icol] );
      fflush(stdout);
    }

  } // icol loop


  // make sure that required keys exist.
  if ( itype == ITYPE_SNFITSIO_HEAD ) 
    {  check_required_headkeys(); }

} // end of function


// ================================
void rd_snfitsio_free(int ifile, int itype ) {

  // free memory in reverse order to how it was allocated.
  // Oct 17 2012 set   MALLOC_LEN_SNFITSIO[itype] = 0 ; 

  int iform, ipar, npar, LEN, i ;

  // --------------- BEGIN ----------

  LEN = MALLOC_LEN_SNFITSIO[itype] ;
  if ( LEN <= 0 ) { return ; }

  printf("\t Free allocated SNFITSIO memory for %s \n",
	 snfitsFile[ifile][itype] );
  fflush(stdout);

  for ( iform=1; iform < MXFORM_SNFITSIO; iform++ ) {

    npar = RD_SNFITSIO_TABLEVAL[itype].NPAR[iform] ; 

    if ( npar <= 0 ) { continue ; }

    for ( ipar=0; ipar <= npar; ipar++ ) {

      /*
      printf(" xxx FREE itype=%d  iform=%d  ipar=%d \n",itype,iform,ipar);
      fflush(stdout);
      */

      if ( iform == IFORM_A ) {	 
	for ( i=0; i <= LEN; i++ )
	  { free (RD_SNFITSIO_TABLEVAL_A[itype][ipar][i]); }
	
	free ( RD_SNFITSIO_TABLEVAL_A[itype][ipar]  ) ;
      }     
      else if ( iform == IFORM_1J ) {
	free ( RD_SNFITSIO_TABLEVAL_1J[itype][ipar] ) ;
      }
      else if ( iform == IFORM_1I ) {
	free ( RD_SNFITSIO_TABLEVAL_1I[itype][ipar] ) ;
      }
      else if ( iform == IFORM_1E ) {
	free ( RD_SNFITSIO_TABLEVAL_1E[itype][ipar] ) ;
      }
      else if ( iform == IFORM_1D ) {
	free ( RD_SNFITSIO_TABLEVAL_1D[itype][ipar] ) ;
      }
      else if ( iform == IFORM_1K ) {
	free ( RD_SNFITSIO_TABLEVAL_1K[itype][ipar] ) ;
      }
      
    } // ipar
  } // iform

  free ( RD_SNFITSIO_TABLEVAL_A[itype]  ) ;
  free ( RD_SNFITSIO_TABLEVAL_1J[itype] ) ;
  free ( RD_SNFITSIO_TABLEVAL_1I[itype] ) ;
  free ( RD_SNFITSIO_TABLEVAL_1E[itype] ) ;
  free ( RD_SNFITSIO_TABLEVAL_1D[itype] ) ;
  free ( RD_SNFITSIO_TABLEVAL_1K[itype] ) ;

  MALLOC_LEN_SNFITSIO[itype] = 0 ; // Oct 17, 2012

} // end of rd_snfitsio_free


// ================================
void rd_snfitsio_malloc(int ifile, int itype, int LEN ) {

  // allocate memory to read table of 'type'.
  // Input LEN is the number of rows to allocate.
  //
  // Jun 17 2018: if LEN==0, set to 10 and avoid abort.

  int LEN_LOCAL = LEN ;

  int 
    iform, npar, ipar, i, NPARTOT
    ,mem, MEM, MSTR, MEMTOT, sizeof_mem, sizeof_MEM
    ;

  float FMEM ;
  fitsfile *fp ;
  char  fnam[] = "rd_snfitsio_malloc"  ;

  // ------------ BEGIN --------------


  // avoid re-allocating if already allocated.
  // Call _free routine first.

  if ( MALLOC_LEN_SNFITSIO[itype] > 0 ) 
    { rd_snfitsio_free(ifile,itype); }


  if (LEN_LOCAL == 0 ) { LEN_LOCAL = 10 ; }

  fp     = fp_snfitsFile[itype] ;
  MEMTOT = NPARTOT = 0 ;

  for ( iform=1; iform < MXFORM_SNFITSIO; iform++ ) {

    // get number of parameters stored with this form
    npar = RD_SNFITSIO_TABLEVAL[itype].NPAR[iform] ; 
    NPARTOT += npar ;

    if ( npar <= 0 ) { continue ; }


    if ( iform == IFORM_A ) {

      sizeof_mem = sizeof(char*) ;
      sizeof_MEM = sizeof(char*) ;
      mem = (npar+1) * sizeof_mem;
      MEM = (LEN_LOCAL+1)  * sizeof_MEM;
      MSTR  = 40 ;

      RD_SNFITSIO_TABLEVAL_A[itype] = (char***)malloc(mem); 

      for ( ipar=0; ipar <= npar; ipar++ ) {
	RD_SNFITSIO_TABLEVAL_A[itype][ipar] = (char**)malloc(MEM); 
	for ( i=0; i <= LEN_LOCAL; i++ ) {
	  RD_SNFITSIO_TABLEVAL_A[itype][ipar][i] = (char*)malloc(MSTR); 
	  MEMTOT += MSTR ;
	}
      }

    }

    else if ( iform == IFORM_1J ) {
      sizeof_mem = sizeof(int*);
      sizeof_MEM = sizeof(int);
      mem = (npar+1)* sizeof_mem;
      MEM = (LEN_LOCAL+1) * sizeof_MEM ;

      RD_SNFITSIO_TABLEVAL_1J[itype] = (int**)malloc(mem); 
      for ( ipar=0; ipar <= npar; ipar++ ) {
	RD_SNFITSIO_TABLEVAL_1J[itype][ipar] = (int*)malloc(MEM);
	MEMTOT += MEM ;
      }
    }
    
    else if ( iform == IFORM_1I ) {
      sizeof_mem = sizeof(short*);
      sizeof_MEM = sizeof(short);
      mem  = (npar+1) * sizeof_mem;
      MEM  = (LEN_LOCAL+1)  * sizeof_MEM;
      RD_SNFITSIO_TABLEVAL_1I[itype] = (short**)malloc(mem); 
      for ( ipar=0; ipar <= npar; ipar++ ) {
	RD_SNFITSIO_TABLEVAL_1I[itype][ipar] = (short*)malloc(MEM); 
	MEMTOT += MEM ;
      }
    }
    
    else if ( iform == IFORM_1E ) {
      sizeof_mem = sizeof(float*);
      sizeof_MEM = sizeof(float);
      mem = (npar+1) * sizeof_mem;
      MEM = (LEN_LOCAL+1)  * sizeof_MEM;
      RD_SNFITSIO_TABLEVAL_1E[itype] = (float**)malloc(mem); 
      for ( ipar=0; ipar <= npar; ipar++ ) {
	RD_SNFITSIO_TABLEVAL_1E[itype][ipar] = (float*)malloc(MEM); 
	MEMTOT += MEM ;
      }
    }

    else if ( iform == IFORM_1D ) {
      sizeof_mem = sizeof(double*);
      sizeof_MEM = sizeof(double);
      mem = (npar+1) * sizeof_mem ;
      MEM = (LEN_LOCAL+1)  * sizeof_MEM ;
      RD_SNFITSIO_TABLEVAL_1D[itype] = (double**)malloc(mem); 
      for ( ipar=0; ipar <= npar; ipar++ ) {
	RD_SNFITSIO_TABLEVAL_1D[itype][ipar] = (double*)malloc(MEM);
	MEMTOT += MEM ;
      }
    }

    else if ( iform == IFORM_1K ) {
      sizeof_mem = sizeof(long long*);
      sizeof_MEM = sizeof(long long);
      mem = (npar+1)* sizeof_mem;
      MEM = (LEN_LOCAL+1) * sizeof_MEM ;

      RD_SNFITSIO_TABLEVAL_1K[itype] = (long long**)malloc(mem); 
      for ( ipar=0; ipar <= npar; ipar++ ) {
	RD_SNFITSIO_TABLEVAL_1K[itype][ipar] = (long long*)malloc(MEM);
	MEMTOT += MEM ;
      }
    }

    else {
      sprintf(c1err,"Unknown iform = %d", iform );
      sprintf(c2err,"%s", "    ");
      errmsg(SEV_FATAL, 0, fnam, c1err, c2err); 
    }

    /*
    printf("\t xxx size(IFORM=%d) = %d(mem)  %d(MEM) \n", 
	   iform, sizeof_mem, sizeof_MEM);
    */

  }  // iform


  MALLOC_LEN_SNFITSIO[itype] = LEN_LOCAL ;  // store length to free later

  // print summary of allocated memory
  FMEM = 1.0E-6*(float)(MEMTOT) ;  
  printf("   Allocated %6.3f MB of memory for %s table (LEN=%d). \n", 
	 FMEM, snfitsType[itype], LEN_LOCAL );
  fflush(stdout); 

  return ;

} // end of rd_snfitsio_malloc



// ================================
void rd_snfitsio_tblcol(int itype, int icol, int firstRow, int lastRow) {

  // Read table column 'icol' and load structure
  // RD_SNFITSIO_TABLEVAL[itype].value_FORM[ipar][1], 
  //
  // Feb 20 2013:  fits_read_col_usht -> fits_read_col_sht
  //

  long NROW, FIRSTROW, FIRSTELEM ;
  int  istat, iform, ipar, anynul ;
  fitsfile *fp ;
  char fnam[] = "rd_snfitsio_tblcol"  ;

  // ------------ BEGIN --------------

  fp    = fp_snfitsFile[itype] ;

  // Now read each column into the appopriate memory for its type.  
  FIRSTROW  = firstRow;  
  FIRSTELEM = 1 ;  
  NROW      = lastRow - firstRow + 1 ;

  iform     = SNFITSIO_TABLEDEF[itype].iform[icol];
  istat = 0;

  // get sparse ipar for this form.
  ipar =     RD_SNFITSIO_TABLEVAL[itype].IPARINV[iform][icol] ;


  if ( iform == IFORM_A ) {
    fits_read_col_str(fp, icol, FIRSTROW, FIRSTELEM, NROW, NULL_A,
		      &RD_SNFITSIO_TABLEVAL_A[itype][ipar][1], 
		      &anynul, &istat );
  }
  else if ( iform == IFORM_1J ) {
    fits_read_col_int(fp, icol, FIRSTROW, FIRSTELEM, NROW, NULL_1J,
		      &RD_SNFITSIO_TABLEVAL_1J[itype][ipar][1], 
		      &anynul, &istat );
  }
  else if ( iform == IFORM_1I ) {
    // usht -> sht (Feb 20 2013)
    fits_read_col_sht(fp, icol, FIRSTROW, FIRSTELEM, NROW, NULL_1I,
		      &RD_SNFITSIO_TABLEVAL_1I[itype][ipar][1], 
		      &anynul, &istat );
  }  
  else if ( iform == IFORM_1E ) {
    fits_read_col_flt(fp, icol, FIRSTROW, FIRSTELEM, NROW, NULL_1E,
		      &RD_SNFITSIO_TABLEVAL_1E[itype][ipar][1], 
		      &anynul, &istat );
  }
  else if ( iform == IFORM_1D ) {
    fits_read_col_dbl(fp, icol, FIRSTROW, FIRSTELEM, NROW, NULL_1D,
		      &RD_SNFITSIO_TABLEVAL_1D[itype][ipar][1], 
		      &anynul, &istat );    
  }
  else if ( iform == IFORM_1K ) {
    fits_read_col_lnglng(fp, icol, FIRSTROW, FIRSTELEM, NROW, NULL_1K,
		      &RD_SNFITSIO_TABLEVAL_1K[itype][ipar][1], 
		      &anynul, &istat );
  }

  else {
    sprintf(c1err,"Invalid iform = %d", iform);
    sprintf(c2err,"itype=%d  icol=%d", itype, icol);
    errmsg(SEV_FATAL, 0, fnam, c1err, c2err); 
  }
  
  

} // end of rd_snfitsio_tblcol



// ================================
void rd_snfitsio_head(int ifile) {

  // Read entire header for all columns, and store.
  // Also fill MXOBS_SNFITSIO 
  //

  int  itype,  icol, ipar, isn, NOBS, NCOL, NSNLC  ;
  fitsfile *fp ;
  //  char  fnam[] = "rd_snfitsio_head" ;

  // ------------ BEGIN --------------

  NSNLC = NSNLC_SNFITSIO[ifile] ; 
  itype = ITYPE_SNFITSIO_HEAD ;
  fp    = fp_snfitsFile[itype] ;

  NCOL = NPAR_SNFITSIO[itype];
  for ( icol=1; icol <= NCOL; icol++ ) {
    rd_snfitsio_tblcol ( itype, icol, 1, NSNLC ); 
  }


  // find largest NOBS = MXOBS_SNFITSIO
  ipar = IPARFORM_SNFITSIO(1, IFORM_1J, "NOBS", itype) ;
  MXOBS_SNFITSIO = 0 ;
  for ( isn = 1; isn <= NSNLC; isn++ ) {
    NOBS = RD_SNFITSIO_TABLEVAL_1J[itype][ipar][isn] ;
    if ( NOBS > MXOBS_SNFITSIO ) { MXOBS_SNFITSIO = NOBS; }
  }
  //  printf("\t Max NOBS = %d \n", MXOBS_SNFITSIO );


} // end of rd_snfitsio_head


// =================================
void check_required_headkeys(void) {

  
#define MXPARREQ_SNFITSIO 20

  int  NREQ, ireq, itype, NERR   ;
  char *ptrReq, REQUIRED_HEADKEYS[MXPARREQ_SNFITSIO][20] ;
  char  fnam[] = "check_required_headkeys"  ;

  // ----------- BEGIN -----------

  NREQ = 0;
  itype = ITYPE_SNFITSIO_HEAD ;

  // hard-wire list of required keys

  NREQ++ ;  ptrReq = REQUIRED_HEADKEYS[NREQ] ;
  sprintf(ptrReq, "%s", "SNID" );
  IPAR_SNFITSIO_SNID       = IPAR_SNFITSIO(0,ptrReq,itype);

  NREQ++ ;  ptrReq = REQUIRED_HEADKEYS[NREQ] ;
  sprintf(ptrReq, "%s", "FAKE" );
  IPAR_SNFITSIO_FAKE       = IPAR_SNFITSIO(0,ptrReq,itype);

  NREQ++ ;  ptrReq = REQUIRED_HEADKEYS[NREQ] ;
  sprintf(ptrReq, "%s", "NOBS" );
  IPAR_SNFITSIO_NOBS    = IPAR_SNFITSIO(0,ptrReq,itype);

  NREQ++ ;  ptrReq = REQUIRED_HEADKEYS[NREQ] ;
  sprintf(ptrReq, "%s", "PTROBS_MIN" );   // start LC pointer in PHOT file
  IPAR_SNFITSIO_PTROBS_MIN = IPAR_SNFITSIO(0,ptrReq,itype);

  NREQ++ ;  ptrReq = REQUIRED_HEADKEYS[NREQ] ;
  sprintf(ptrReq, "%s", "PTROBS_MAX" );
  IPAR_SNFITSIO_PTROBS_MAX = IPAR_SNFITSIO(0,ptrReq,itype);

  if ( NREQ >= MXPARREQ_SNFITSIO ) {
    sprintf(c1err,"NREQ = %d exceeds bound.", NREQ);
    sprintf(c2err,"Check MXPARREQ_SNFITSIO = %d", MXPARREQ_SNFITSIO );
    errmsg(SEV_FATAL, 0, fnam, c1err, c2err); 
  }


  // -----------
  // Check that all required header keys were found;
  // print non-existant keys to screen.

  NERR = 0;
  for ( ireq = 1; ireq <= NREQ; ireq++ ) {
    ptrReq = REQUIRED_HEADKEYS[ireq] ;
    if ( IPAR_SNFITSIO(0,ptrReq,itype) < 0 ) {
      NERR++ ;
      printf(" ERROR: missing required header key '%s' \n", ptrReq );
    }
  }

  // abort if any required keys are missing.
  if ( NERR > 0 ) {
    sprintf(c1err,"Missing %d required headers keys.", NERR );
    sprintf(c2err,"%s", "See list printed avove.");
    errmsg(SEV_FATAL, 0, fnam, c1err, c2err); 
  }



} // end of check_required_headkeys


// ===================================
void  rd_snfitsio_specFile( int ifile ) {

  // April 2019
  // Open and SPECTROGRAPH file and read first two tables
  // to prepare for reading arbitrary spectrum from 3rd table.

  int istat, itype, hdutype, icol, anynul, MEMD, ilam ;
  int nmove=1;
  long FIRSTROW=1, FIRSTELEM=1, NROW ;
  fitsfile *fp ;
  char *ptrFile, keyName[40], comment[200] ;
  char fnam[] = "rd_snfitsio_spec" ;

  // ------------ BEGIN -------------

  if ( !SNFITSIO_SIMFLAG_SPECTROGRAPH ) { return; }
  
  istat = 0;
  itype   = ITYPE_SNFITSIO_SPEC ;
  ptrFile = snfitsFile_plusPath[ifile][itype];

  // open SPEC fits file for reading
  fits_open_file(&fp_snfitsFile[itype], ptrFile, READONLY, &istat );
  sprintf(c1err,"Open %s", snfitsFile[ifile][itype] );
  snfitsio_errorCheck(c1err, istat);
  fp      = fp_snfitsFile[itype] ;  

  printf("\n");
  printf("   Open %s  \n",  snfitsFile[ifile][itype] ); 

  // - - - - - - - - - - - - - - - - - - - - - - -
  // move to table with wave binning
  fits_movrel_hdu( fp, nmove, &hdutype, &istat );
  sprintf(c1err,"movrel to %s table", snfitsType[itype] ) ;
  snfitsio_errorCheck(c1err, istat);


  if ( ifile==1 ) { 
    // read number of lambda bins on first file only
    sprintf(keyName, "%s", "NAXIS2" );
    fits_read_key(fp, TLONG, keyName,  &NROW, comment, &istat );
    printf("   Read %ld wavelength bins.\n", NROW );  fflush(stdout);
    RDSPEC_SNFITSIO_LAMINDEX.NLAMBIN = NROW ;
  
    // malloc lam arrays
    MEMD = NROW * sizeof(double);
    RDSPEC_SNFITSIO_LAMINDEX.LAMMIN_LIST = (double*) malloc(MEMD);
    RDSPEC_SNFITSIO_LAMINDEX.LAMMAX_LIST = (double*) malloc(MEMD);   
  
    icol=2;
    fits_read_col_dbl(fp, icol, FIRSTROW, FIRSTELEM, NROW, NULL_1D,
		      RDSPEC_SNFITSIO_LAMINDEX.LAMMIN_LIST, 
		      &anynul, &istat );    
    icol=3;
    fits_read_col_dbl(fp, icol, FIRSTROW, FIRSTELEM, NROW, NULL_1D,
		      RDSPEC_SNFITSIO_LAMINDEX.LAMMAX_LIST,
		      &anynul, &istat );    
    
  } // end ifile=1


  // - - - - - - - - - - - - - - - - - - - - - - - - - - - 
  // always read and store one-row-per spectrum; note that 
  // a given SNID can have multiple spectra and thus multiple rows.

  // move to next table : HEADER (one row per spectrum)
  fits_movrel_hdu( fp, nmove, &hdutype, &istat );
  sprintf(c1err,"movrel to %s table", snfitsType[itype] ) ;
  snfitsio_errorCheck(c1err, istat);

  if ( RDSPEC_SNFITSIO_HEADER.NROW > 0 ) { rd_snfitsio_mallocSpec(-1); }

  // read number of HEADER rows
  sprintf(keyName, "%s", "NAXIS2" );
  fits_read_key(fp, TLONG, keyName,  &NROW, comment, &istat );
  printf("   Read %ld SPECTRUM-HEADER rows.\n", NROW);  fflush(stdout);
  RDSPEC_SNFITSIO_HEADER.NROW = NROW ;

  // if spectrograph table exists but there are no sim spectra,
  // then turn of SPECTROGRAPH flag and bail -> no further attemp
  // to read spectra.
  if ( NROW == 0 ) 
    { SNFITSIO_SIMFLAG_SPECTROGRAPH = false ; return; }

  rd_snfitsio_mallocSpec(+1);


  icol=1 ;
  fits_read_col_str(fp, icol, FIRSTROW, FIRSTELEM, NROW, NULL_A,
		    RDSPEC_SNFITSIO_HEADER.SNID, &anynul, &istat ); 
  icol=2 ;
  fits_read_col_dbl(fp, icol, FIRSTROW, FIRSTELEM, NROW, NULL_1D,
		    RDSPEC_SNFITSIO_HEADER.MJD, &anynul, &istat ); 

  icol=3 ;
  fits_read_col_flt(fp, icol, FIRSTROW, FIRSTELEM, NROW, NULL_1E,
		    RDSPEC_SNFITSIO_HEADER.TEXPOSE, &anynul, &istat ); 

  icol=7 ;
  fits_read_col_int(fp, icol, FIRSTROW, FIRSTELEM, NROW, NULL_1I,
		    RDSPEC_SNFITSIO_HEADER.NLAMBIN, &anynul, &istat ); 

  icol=8 ;
  fits_read_col_int(fp, icol, FIRSTROW, FIRSTELEM, NROW, NULL_1J,
		    RDSPEC_SNFITSIO_HEADER.PTRSPEC_MIN, &anynul, &istat ); 

  icol=9 ;
  fits_read_col_int(fp, icol, FIRSTROW, FIRSTELEM, NROW, NULL_1J,
		    RDSPEC_SNFITSIO_HEADER.PTRSPEC_MAX, &anynul, &istat ); 

  // move to next table : SPECTRAL FLUX vs. wave
  fits_movrel_hdu( fp, nmove, &hdutype, &istat );
  sprintf(c1err,"movrel to %s table", snfitsType[itype] ) ;
  snfitsio_errorCheck(c1err, istat);

  return ;

} // end rd_snfitsio_specFile


// ====================================
int RD_SNFITSIO_SPECROWS(char *SNID, int *ROWMIN, int *ROWMAX)  { 

  // Apr 2019
  // read spectra-HEADER table rows for this SNID, 
  // and return min and max rownum.
  // Inputs:
  //   *SNID   : SNID to search
  //
  // Outputs
  //   *ROWMIN, *ROWMAX : min and max rows for spectra
  //
  // Function returns number of spectra for this SNID (Mar 4 2021)
  
  int NROW = RDSPEC_SNFITSIO_HEADER.NROW; 
  int irow, NSPEC = 0 ;
  char *SNID_TMP;
  //  char fnam[] = "RD_SNFITSIO_SPECROWS" ;

  // ------------ BEGIN -------------

  *ROWMIN = *ROWMAX = -9 ;

  for(irow=0; irow < NROW; irow++ ) {
    SNID_TMP = RDSPEC_SNFITSIO_HEADER.SNID[irow];
    if ( strcmp(SNID_TMP,SNID) == 0 ) {
      NSPEC++ ;
      if ( *ROWMIN < 0 ) { *ROWMIN = irow; }
      *ROWMAX = irow;
    }
  }

  
  //  printf(" xxx %s: SNID=%s  ROW-range = %d to %d \n",
  //	 fnam, SNID, *ROWMIN, *ROWMAX );
  
  return(NSPEC) ;
  
} // end RD_SNFITSIO_SPECROWS

void rd_snfitsio_specrows__(char *SNID, int *ROWMIN, int *ROWMAX)  { 
  RD_SNFITSIO_SPECROWS(SNID,ROWMIN,ROWMAX);
  return ;
}

// =========================================================
void RD_SNFITSIO_SPECDATA(int irow, 
			  double *LAMMIN, double *LAMMAX, 
			  double *FLAM, double *FLAMERR, double *SIM_FLAM) {

  // Read spectral data for input 'irow'.
  // Returns:
  //  LAMMIN,LAMMAX =  array of min/max wave in each bin
  //  FLAM,FLAMERR  = flux and error in each wave bin
  //
  // For sim, also return true GENFLAM

  char fnam[] = "RD_SNFITSIO_SPECDATA";
  if ( irow < 0 ) {
    sprintf(c1err,"Invalid irow = %d", irow);
    sprintf(c2err,"Valid irow must be > 0 ");
    errmsg(SEV_FATAL, 0, fnam, c1err, c2err); 
  }
  fitsfile *fp = fp_snfitsFile[ITYPE_SNFITSIO_SPEC] ;  
  int NLAM     = RDSPEC_SNFITSIO_HEADER.NLAMBIN[irow] ;
  int PTRMIN   = RDSPEC_SNFITSIO_HEADER.PTRSPEC_MIN[irow];
  int PTRMAX   = RDSPEC_SNFITSIO_HEADER.PTRSPEC_MAX[irow];
  int itype    = ITYPE_SNFITSIO_SPEC;

  int *LAMINDEX, MEMI, istat=0, icol, anynul, ilam, ILAM ; 
  long NROW      = PTRMAX - PTRMIN + 1;
  long FIRSTROW  = PTRMIN ;
  long FIRSTELEM = 1 ;

  int  LDMP = 0 ;

  // --------------- BEGIN --------------

  // allocate LAMINDEX array
  MEMI     = NLAM * sizeof(int);
  LAMINDEX = (int*) malloc(MEMI);

  if ( LDMP ) {
    printf(" 0. xxx %s ---------------------------- \n", fnam);
    printf(" 1. xxx %s ROW=%d \n", fnam, irow );
  }

  icol=1 ;  
  fits_read_col_int(fp, icol, FIRSTROW, FIRSTELEM, NROW, NULL_1J,
		    LAMINDEX, &anynul, &istat ); 
  sprintf(c1err,"Read LAMINDEX for spectra", snfitsType[itype] ) ;
  snfitsio_errorCheck(c1err, istat);

  if(LDMP) { printf(" 2. xxx %s  NLAM=%d \n", fnam, NLAM ); }

  // transfer LAMINDEX to LAMMIN & LAMMAX
  for(ilam=0; ilam < NLAM; ilam++ ) {
    ILAM         = LAMINDEX[ilam];
    LAMMIN[ilam] = RDSPEC_SNFITSIO_LAMINDEX.LAMMIN_LIST[ILAM] ;
    LAMMAX[ilam] = RDSPEC_SNFITSIO_LAMINDEX.LAMMAX_LIST[ILAM] ;

    if ( LDMP ) {
      if ( ilam < 5 ) {
	printf("\t xxx ilam=%d  ILAM=%d  LAMIN/MAX=%f/%f\n", 
	       ilam, ILAM, LAMMIN[ilam], LAMMAX[ilam] );
      }
    } // end LDMP
  }


  icol=2 ;  istat=0;
  fits_read_col_dbl(fp, icol, FIRSTROW, FIRSTELEM, NROW, NULL_1D,
		    FLAM, &anynul, &istat ); 

  icol=3 ;  istat=0;
  fits_read_col_dbl(fp, icol, FIRSTROW, FIRSTELEM, NROW, NULL_1D,
		    FLAMERR, &anynul, &istat ); 

  icol=4 ;  istat=0;
  fits_read_col_dbl(fp, icol, FIRSTROW, FIRSTELEM, NROW, NULL_1D,
		    SIM_FLAM, &anynul, &istat ); 

  // printf(" 4. xxx %s\n", fnam);

  free(LAMINDEX);
  return ;

} // end RD_SNFITSIO_SPECFLUX


// =========================================
void  rd_snfitsio_mallocSpec(int opt) {

  // opt > 0 -> malloc RDSPEC_SNFITSIO_HEADER
  // opt < 0 -> free   RDSPEC_SNFITSIO_HEADER

  int  NROW  =   RDSPEC_SNFITSIO_HEADER.NROW;
  int  MEMD  =   NROW * sizeof(double);
  int  MEMI  =   NROW * sizeof(int);
  int  MEMF  =   NROW * sizeof(float);
  int  MEMC  =   NROW * sizeof(char*);
  int  MEMSNID = 40   * sizeof(char) ;
  int irow;
  //  char fnam[] = "rd_snfitsio_mallocSpec" ;

  // ------------ BEGIN -----------

  if ( opt > 0 ) {
    
    RDSPEC_SNFITSIO_HEADER.MJD     = (double *) malloc (MEMD) ;
    RDSPEC_SNFITSIO_HEADER.TEXPOSE = (float  *) malloc (MEMF) ;
    RDSPEC_SNFITSIO_HEADER.NLAMBIN = (int    *) malloc (MEMI) ;
    RDSPEC_SNFITSIO_HEADER.PTRSPEC_MIN = (int    *) malloc (MEMI) ;
    RDSPEC_SNFITSIO_HEADER.PTRSPEC_MAX = (int    *) malloc (MEMI) ;

    RDSPEC_SNFITSIO_HEADER.SNID  = (char**) malloc (MEMC) ;
    for(irow=0; irow<NROW; irow++ ) 
      { RDSPEC_SNFITSIO_HEADER.SNID[irow] = (char*) malloc(MEMSNID);}

  }
  else {
    free(RDSPEC_SNFITSIO_HEADER.MJD);
    free(RDSPEC_SNFITSIO_HEADER.TEXPOSE);
    free(RDSPEC_SNFITSIO_HEADER.NLAMBIN );
    free(RDSPEC_SNFITSIO_HEADER.PTRSPEC_MIN );
    free(RDSPEC_SNFITSIO_HEADER.PTRSPEC_MAX );

    for(irow=0; irow<NROW; irow++ ) 
      { free(RDSPEC_SNFITSIO_HEADER.SNID[irow] ); }
    free(RDSPEC_SNFITSIO_HEADER.SNID);
  }


  return ;

} // end rd_snfitsio_mallocSpec


// ==================================
int formIndex_snfitsio(char *form) {

  // Return index corresponding to the input table form.
  // Input *form can be "1D", "12A", etc ...
  
  int  LFORM ;
  char lastchar[2];
  char fnam[] = "formIndex_snfitsio" ;

  // ----------- BEGIN ------------

  LFORM = strlen(form);
  sprintf(lastchar, "%c", *(form+LFORM-1) );

  if ( strcmp(lastchar,"A") == 0 ) {
    return IFORM_A ;
  }
  else if ( strcmp(form,"1J") == 0 ) {
    return IFORM_1J ;
  }
  else if ( strcmp(form,"1I") == 0 ) {
    return IFORM_1I ;
  }
  else if ( strcmp(form,"1E") == 0 ) {
    return IFORM_1E ;
  }
  else if ( strcmp(form,"1D") == 0 ) {
    return IFORM_1D ;
  }
  else if ( strcmp(form,"1K") == 0 ) {
    return IFORM_1K ;
  }
  else {
    sprintf(c1err,"Unrecognized fits-form '%s'", form);
    sprintf(c2err,"Check fits table.");
    errmsg(SEV_FATAL, 0, fnam, c1err, c2err); 
  }

  return -9 ;

} // end of formIndex


// ==========================================
void SET_RDMASK_SNFITSIO(int NEP, int *mask) {

  // set Read-mask so that RD_SNFITSIO_PARVAL only
  // saves the epochs set in *mask. If NEP=0 then 
  // the mask is turned off and *mask is ignored.
  //

  int ep, JVAL;
  char fnam[] = "SET_RDMASK_SNFITSIO" ;

  // ------------ BEGIN --------------

  if ( NEP >= MXEPOCH ) {
      sprintf(c1err,"NEP = %d at exceeds bound.", NEP);
      sprintf(c2err,"Check MXEPOCH = %d", MXEPOCH);
      errmsg(SEV_FATAL, 0, fnam, c1err, c2err); 
  }

  NEP_RDMASK_SNFITSIO_PARVAL  = NEP;

  
  if ( NEP == 0 ) { return ; }

  for ( ep=0; ep < NEP; ep++ ) {
    JVAL = mask[ep];

    // make sure that each  mask value is 0 or 1
    if ( JVAL != 0 && JVAL != 1 ) {
      sprintf(c1err,"Invalid MASK = %d at ep=%d", JVAL, ep);
      sprintf(c2err,"%s", "        ");
      errmsg(SEV_FATAL, 0, fnam, c1err, c2err); 
    }

    RDMASK_SNFITSIO_PARVAL[ep] = JVAL ;
  }

} // end of SET_RDMASK_SNFITSIO

void set_rdmask_snfitsio__(int *N, int *mask) {
  SET_RDMASK_SNFITSIO(*N, mask) ;
}


// ============================================
int RD_SNFITSIO_PARVAL(int     isn        // (I) internal SN index   
		      ,char   *parName    // (I) name of PARAM
		      ,double *parList    // (O) value(s) of PARAM
		      ,char   *parString  // (O) string values of PARAM
		      ,int    *iptr       // (O) pointer to PARAM
		      ) {

  //
  // Generic fits-read function to return the value(s) for
  // input parameter *parName.  If the parameter is int, float
  // or double, then double *parList is filled and *parString
  // is NULL. If the  parameter cast is character (such as SNID)
  // then *parString is fill and *parList is ignored.
  // The return value of the function is the number of elements
  // filled;  1 for a header value or NOBS for a 'phot' value
  // such as MJD or FLUXCAL. If the parameter does not exist
  // then zero is returned.
  //
  // For *parString a blank space separates each element:
  // for example, the NOBS filters are returned as
  //  *parString = "u g r i z u g r i z ..."
  // Thus the calling function must split *parString to 
  // read the elements.  The motivation for this strategy
  // is to allow fortran calls. Make sure to pass sufficiently
  // large *parString and *parList arrays.
  // The last return value *iptr allows faster lookup
  // on subsequent calls to avoid the slow string searches
  // to match *parName to one of the stored parameter names.
  //
  // May 5 2014: 
  //   Fix subtle bug; on first SN in each file, reset *iptr = -9
  //   so that new header is searched for each parameter.
  //   Now works properly with Ia and CC file in same version.
  //
  // Feb 11 2021: parString is now comma-sep instead of space-sep
  //         --> allows other functions to use parse_commaSep function
  //
  int  
    iptr_local, iform, itype, ifile, itmp
    ,icol, ipar, NSTORE
    ,iparRow
    ,isn_file
    ,firstRow, lastRow
    ,NPARVAL
    ,J, JMIN, JMAX, NSTR=0
    ,*IPTR
    ,MASK, NEP_RDMASK, NEP_MASK, LDMP 
    ;

  char   C_VAL[80];
  int    J_VAL ;
  float  E_VAL ;
  double D_VAL ;
  long long K_VAL ;

  char fnam[] = "RD_SNFITSIO_PARVAL" ;

  // ------------ BEGIN --------------

  // init output args
  NPARVAL  = 0;  
  *parList = -9.0 ;
  parString[0] = 0 ;

  LDMP = 0 ; // ( strcmp(parName,"SIM_PEAKMAG_i") == 0 ||
  ifile = -9 ;

  // check if we read current fits file, or need to open the next one.
  for ( itmp = 1; itmp <= NFILE_SNFITSIO; itmp++ ) {
    if ( isn >  NSNLC_SNFITSIO_SUM[itmp-1] &&
	 isn <= NSNLC_SNFITSIO_SUM[itmp] ) 
      { ifile = itmp ; }
  }


  if ( ifile != IFILE_SNFITSIO ) {
    RD_SNFITSIO_CLOSE(SNFITSIO_PHOT_VERSION) ;
    IFILE_SNFITSIO    = ifile ;           // update global file index
    ISNFIRST_SNFITSIO = isn ;             // first ISN in file
    rd_snfitsio_file(IFILE_SNFITSIO);     // open next fits file.
    rd_snfitsio_specFile(IFILE_SNFITSIO); // check for spectra (4.2019)
  }

  // get local 'isn_file' index within this file;
  // Note that 'isn' is an absolute index over all files.
  isn_file = isn - NSNLC_SNFITSIO_SUM[IFILE_SNFITSIO-1];

  // determine 'itype' and 'icol' from parName.
  // use pointer if it's defined (i.e, positive); otherwise search list.
  // Search list if this is the first SN in the file in case the
  // header has changed.

  
  if ( isn == ISNFIRST_SNFITSIO ) { *iptr = -9 ; } // May 5 2014

  iptr_local = *iptr ;

  if ( iptr_local == -999 ) 
    {  return 0 ;  }
  
  else {

    // search list of all param-names
    for ( itype=0; itype <= 1; itype++ ) { // check HEAD and PHOT 
      icol = IPAR_SNFITSIO(0, parName, itype) ;
      if ( icol > 0 ) { 
	*iptr = icol + itype*MXPAR_SNFITSIO ;
	goto FOUND_COLUMN ; 
      }
    }

    *iptr = -999 ; // return flag that this param does not exist
    return 0 ;

    /*
    sprintf(c1err,"No table column for parName = '%s'", parName);
    sprintf(c2err,"           ");
    errmsg(SEV_FATAL, 0, fnam, c1err, c2err); 
    */

  }


 FOUND_COLUMN:
  iform =  SNFITSIO_TABLEDEF[itype].iform[icol] ;
  ipar =   RD_SNFITSIO_TABLEVAL[itype].IPARINV[iform][icol] ; // sparse ipar


  /*
  printf(" xxxx isn=%d  parName=%-10s : icol=%2d  itype=%d  iform=%d \n",
	 isn, parName, icol, itype, iform );
  */

  if ( LDMP ) {
    printf(" xxxx ------------------------------------------ \n" );
    printf(" xxxx %s : icol=%d, itype=%d, iform=%d  ipar=%d\n",
	   parName, icol, itype, iform, ipar );
    printf(" xxxx iptr_local=%d  isn=%d  ISNFIRST=%d \n",
	   iptr_local, isn, ISNFIRST_SNFITSIO ) ;
    printf(" xxxx ifile=%d  IFILE_SNFITSIO=%d \n",
	   ifile, IFILE_SNFITSIO );

    fflush(stdout);
  }


  // if type is PHOT then read the epoch range from the fits file.
  if ( itype == ITYPE_SNFITSIO_PHOT ) {
    
    IPTR = RD_SNFITSIO_TABLEVAL[ITYPE_SNFITSIO_HEAD].IPARINV[IFORM_1J] ; 

    iparRow = *(IPTR+IPAR_SNFITSIO_PTROBS_MIN) ; 
    firstRow = 
      RD_SNFITSIO_TABLEVAL_1J[ITYPE_SNFITSIO_HEAD][iparRow][isn_file]; 

    iparRow = *(IPTR+IPAR_SNFITSIO_PTROBS_MAX) ; 
    lastRow  = 
      RD_SNFITSIO_TABLEVAL_1J[ITYPE_SNFITSIO_HEAD][iparRow][isn_file]; 

    if ( LDMP ) {
      printf(" xxxx %s rows: %d to %d \n", parName, firstRow, lastRow );
      fflush(stdout);
    }

    rd_snfitsio_tblcol( itype, icol, firstRow, lastRow) ;

    NPARVAL = lastRow - firstRow + 1 ;
    JMIN = 1;
    JMAX = NPARVAL ;

    // make sure that the size of the epoch mask is the same
    // as the number of observations.
    NEP_RDMASK = NEP_RDMASK_SNFITSIO_PARVAL  ;
    if ( NEP_RDMASK > 0 && NEP_RDMASK != NPARVAL ) {
      sprintf(c1err,"NEP_RDMASK = %d but NOBS=%d", NEP_RDMASK, NPARVAL);
      sprintf(c2err,"isn=%d  parName=%s", isn, parName );
      errmsg(SEV_FATAL, 0, fnam, c1err, c2err); 
    }

  }
  else {
    NPARVAL  = 1 ;
    JMIN = isn_file ;
    JMAX = isn_file ;
  }

  // read from stored array and load output array 
  // *parList or *parString

  NSTORE = NEP_MASK = 0;
  for ( J = JMIN; J <= JMAX; J++ ) {

    // check mask only for photometry and only if mask is set.
    if ( itype == ITYPE_SNFITSIO_PHOT && NEP_RDMASK_SNFITSIO_PARVAL ) {
      MASK = RDMASK_SNFITSIO_PARVAL[J-1];
      if ( MASK == 0 ) { continue ; }
    }

    NEP_MASK++ ; // increment epochs passing MASK cut

    if ( iform == IFORM_A ) { 
      sprintf(C_VAL,"%s", RD_SNFITSIO_TABLEVAL_A[itype][ipar][J] ); 
      catVarList_with_comma(parString,C_VAL); // flat string, comma-sep

      // clumsy load of epoch-dependent strings here for speed;
      // avoids additional epoch loops later. Note ep starts at 1.
      if ( strcmp(parName,"FLT") == 0 || strcmp(parName,"BAND")==0 ) 
	{ sprintf(SNDATA.FILTCHAR[NSTORE+1],"%s",  C_VAL); }
      if ( strcmp(parName,"FIELD") == 0 ) 
	{ sprintf(SNDATA.FIELDNAME[NSTORE+1],"%s", C_VAL); }

      /* xxx mark delete 
      NSTR++ ;
      if ( NSTR > 1 && strlen(C_VAL)>0 ) { strcat(parString,COMMA); }
      strcat(parString,C_VAL);
      // mark original: strcat(parString, C_VAL);  strcat(parString," "); 
      xxxx */ 
    }
    else if ( iform == IFORM_1J ) { 
      J_VAL = RD_SNFITSIO_TABLEVAL_1J[itype][ipar][J]; 
      *(parList+NSTORE) = (double)J_VAL ;
    }
    else if ( iform == IFORM_1I ) { 
      J_VAL = (int)RD_SNFITSIO_TABLEVAL_1I[itype][ipar][J]; 
      *(parList+NSTORE) = (double)J_VAL ;
    }
    else if ( iform == IFORM_1E ) { 

      // xxxxxxxxxxxxxxxxxxx
      if ( J == -2 ) {
	printf(" XXX (%s) itype=%d  ipar=%d  icol=%2d  J=%d\n", 
	       parName, itype, ipar, icol, J); 
	fflush(stdout);
      }
      // xxxxxxxxxxxxxxxxxxx

      E_VAL = RD_SNFITSIO_TABLEVAL_1E[itype][ipar][J]; 
      *(parList+NSTORE) = (double)E_VAL ;
    }
    else if ( iform == IFORM_1D ) { 
      D_VAL = RD_SNFITSIO_TABLEVAL_1D[itype][ipar][J]; 
      *(parList+NSTORE) = D_VAL ;
    }
    else if ( iform == IFORM_1K ) { 
      K_VAL = RD_SNFITSIO_TABLEVAL_1K[itype][ipar][J]; 
      *(parList+NSTORE) = K_VAL ;
    }

    NSTORE++ ;
  }


  /* xxx
  if ( strcmp(parName,"FIELD")==0  ) 
    { printf(" xxx %s: FIELD -> '%s' \n", fnam, parString); }
  xxxx */

  if ( NEP_MASK == 0 )
    { return -9;     }   // flag that all epochs failed MASK cut
  else
    { return NSTORE ; }

} // end of  RD_SNFITSIO_PARVAL

int rd_snfitsio_parval__(int *isn, char *parName, 
			 double *parLIST, char *parString, int *iptr) {
  return RD_SNFITSIO_PARVAL(*isn, parName, parLIST, parString, iptr);
}


/* ==============================================================
 Below are user functions to return 
    string  ( _STR)
    int     ( _INT)  // signed int
    int     ( _SHT)  // signed short int
    float   ( _FLT)
    double  ( _DBL)

  Each function below simply calls the general RD_SNFITSIO_PARVAL
  function, and then returns the value or array in the appropriate
  cast.
 ======================================================== */


int RD_SNFITSIO_STR(int isn, char *parName, char *parString, int *ipar) {
  int    NRD;
  double tmp8[10] ;
  NRD = RD_SNFITSIO_PARVAL(isn, parName, tmp8, parString, ipar);
  return NRD ;
}
int RD_SNFITSIO_INT(int isn, char *parName, int    *parList, int *ipar) {
  int    i, NRD;
  double tmp8[MXEPOCH] ;
  char   String[20] ;
  NRD = RD_SNFITSIO_PARVAL(isn, parName, tmp8, String, ipar);
  for ( i=0; i < NRD; i++ ) 
    { parList[i] = (int)tmp8[i] ; }
  return NRD ;
}
int RD_SNFITSIO_SHT(int isn, char *parName, short int *parList, int *ipar) {
  int    i, NRD;
  double tmp8[MXEPOCH] ;
  char   String[20] ;
  NRD = RD_SNFITSIO_PARVAL(isn, parName, tmp8, String, ipar);
  for ( i=0; i < NRD; i++ ) 
    { parList[i] = (short int)tmp8[i] ; }
  return NRD ;
}
int RD_SNFITSIO_FLT(int isn, char *parName, float  *parList, int *ipar) {
  int    i, NRD;
  double tmp8[MXEPOCH] ;
  char   String[20] ;
  NRD = RD_SNFITSIO_PARVAL(isn, parName, tmp8, String, ipar);
  for ( i=0; i < NRD; i++ ) 
    { parList[i] = (float)tmp8[i] ; }
  return NRD ;
}
int RD_SNFITSIO_DBL(int isn, char *parName, double *parList, int *ipar) {
  int    NRD;
  char   String[20] ;
  NRD = RD_SNFITSIO_PARVAL(isn, parName, parList, String, ipar);
  return NRD ;
}


int rd_snfitsio_str__(int *isn, char *parName, char *parString, int *iptr) {
  return RD_SNFITSIO_STR(*isn, parName, parString, iptr);
}

int rd_snfitsio_int__(int *isn, char *parName, int *parLIST, int *iptr) {
  return RD_SNFITSIO_INT(*isn, parName, parLIST, iptr);
}

int rd_snfitsio_sht__(int *isn, char *parName, short int *parLIST, int *iptr){
  return RD_SNFITSIO_SHT(*isn, parName, parLIST, iptr);
}

int rd_snfitsio_flt__(int *isn,  char *parName, float *parLIST, int *iptr) {
  return RD_SNFITSIO_FLT(*isn, parName, parLIST, iptr);
}
int rd_snfitsio_dbl__(int *isn,  char *parName, double *parLIST, int *iptr) {
  return RD_SNFITSIO_DBL(*isn, parName, parLIST, iptr);
}
