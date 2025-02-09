#!/usr/bin/env python
#
# Created Mar 19 2021 by R.Kessler
#
# Mini-pipeline to create fluxError corrections for data and sim.
# The following stages are run here:
#  + create SIMLIB from fakes overlaid on images
#  + run simulation using SIMLIB to have same epochs and mags as fakes
#  + create tables with every observation
#  + make fluxError maps
#
#
# TODO
#  - FIELD dependence
#  + apply cuts on NSIG ?
#  + include FILTER dependence
#
# ========================

import os, sys, argparse, glob, yaml, math
import numpy as np
from   argparse import Namespace
import pandas as pd

#JOBNAME_SNANA = "/home/rkessler/SNANA/bin/snana.exe"
JOBNAME_SNANA = "snana.exe"
JOBNAME_SIM   = "snlc_sim.exe"

TABLE_NAME    = "OUTLIER"

STRING_FAKE   = "FAKE"
STRING_SIM    = "SIM"
STRING_FIELDS = "FIELDS"

FLUXERRMODEL_FILENAME_FAKE = f"FLUXERRMODEL_{STRING_FAKE}.DAT"
FLUXERRMODEL_FILENAME_SIM  = f"FLUXERRMODEL_{STRING_SIM}.DAT"
 
USERNAME      = os.environ['USER']
USERNAME4     = os.environ['USER'][0:4]
HOSTNAME      = os.uname()[1].split('.')[0]

COLNAME_BIN1D    = "BIN1D"
COLNAME_IFILTOBS = "IFILTOBS"
COLNAME_BAND     = "BAND"
COLNAME_IFIELD   = "IFIELD"

IFILTOBS_MAX = 80

ISTAGE_MAKEMAP = 4

HELP_CONFIG = """
# keys for input config file

OUTDIR: [OUTDIR]   # name of output directory

VERSION_FAKES:  [full path to data folder with fakes that include true mag]
HOSTLIB_FILE:   [full path to HOSTLIB]
KCOR_FILE:      [full path to KCOR/calib file]

OPT_SIMLIB:  2  # 1=all epochs, 2=only epochs with Ftrue>0

# optional, reject extreme NSIG outliers
CUTWIN_NSIG:  0.0  [NSIG_MAX]

# additional/optional &SNLCINP arguments to select events/epochs
EXTRA_SNLCINP_ARGS:
  - PHOTFLAG_MSKREJ = [MSK]  # reject events with these PHOTFLAG bits
  - OPT_SETPKMJD    = -9     # don't wast time computing PEAKMJD
  - CUTWIN_MJD      = [MJDMIN], [MJDMAX]  # select season(s)
  - CUTWIN_NFIELD   = 1, 1                # reject field overlaps
  - CUTWIN_ERRTEST  = 0.5, 2.0            # reject crazy ERR_CALC/ERR_DATA
  - SIMVAR_CUTWIN_STRING = 'NEP_SIM_MAGOBS 4 9999' # at least 4 Ftrue>0

#  - MXEVT_PROCESS = 500  # quick test with small subset of fakes

# Optional map-computation in independent groups of fields. 
# Here the maps are computed for SHALLOW and DEEP field groups.
FIELDS: 
  SHALLOW:  S1 S2 C1 C2 X1 X2 E1 E2
  DEEP:     X3 C3

# Define multi-dimensional map bins using variable(s) from OUTLIER table.
# Values outside map-bin range are pulled into first/last map bin so 
# that all obs are used.

FLUXERRMAP_BINS: 
  - FILTER                 # auto compute bins from filters in data file
  - SBMAG   8  20   28     # nbin min max (histogram bins)
  - PSF     3  1.0  4.0    # idem

"""

# ==================================

def get_args():
    parser = argparse.ArgumentParser()

    msg = "HELP on input config file"
    parser.add_argument("-H", "--HELP", help=msg, action="store_true")

    msg = "name of input file"
    parser.add_argument("input_file", help=msg, nargs="?", default=None)

    msg = "clobber everything and start over"
    parser.add_argument("--clobber", help=msg, action="store_true")

    msg = f"start stage number (default=1, {ISTAGE_MAKEMAP}=make maps)"
    parser.add_argument("-s", "--start_stage", help=msg, nargs='?', 
                        type=int, default=1)

    msg = f"Jump to make map stage {ISTAGE_MAKEMAP} (previous stages already run)"
    parser.add_argument("-m", "--makemap", help=msg, action="store_true")

    msg = "verify maps"
    parser.add_argument("--verify", help=msg, action="store_true")

    args = parser.parse_args()

    if args.makemap : args.start_stage = ISTAGE_MAKEMAP

    if len(sys.argv) == 1:
        parser.print_help()
        sys.exit()

    return args

    # end get_args

def read_yaml(input_file):
    input_lines = []
    with open(input_file, 'r') as f :
        for line in f:
            input_lines.append(line)

    config_yaml = yaml.safe_load("\n".join(input_lines))
    return config_yaml
    # end read_yaml

def read_input(input_file):

    input_yaml = read_yaml(input_file)

    # parse VERSION_FAKES into path and version for &SNLCINP inputs
    VERSION_FAKES     = input_yaml['VERSION_FAKES']
    PRIVATE_DATA_PATH = os.path.dirname(VERSION_FAKES)
    VERSION           = os.path.basename(VERSION_FAKES)
    input_yaml['VERSION']           = VERSION
    input_yaml['PRIVATE_DATA_PATH'] = PRIVATE_DATA_PATH

    # expand FIELDs into group names and list per group
    FIELD_GROUP_NAMES = []
    FIELD_GROUP_LISTS = []
    if STRING_FIELDS in input_yaml:
        FIELDS = input_yaml[STRING_FIELDS]
        for field_group_name in FIELDS :
            field_list = FIELDS[field_group_name].split()
            FIELD_GROUP_NAMES.append(field_group_name)
            FIELD_GROUP_LISTS.append(field_list)
            #print(f" xxx field group {field_group_name} = {field_list}")

    input_yaml['NFIELD_GROUP']      = len(FIELD_GROUP_NAMES)
    input_yaml['FIELD_GROUP_NAMES'] = FIELD_GROUP_NAMES
    input_yaml['FIELD_GROUP_LISTS'] = FIELD_GROUP_LISTS

    return input_yaml
    # end read_input

def prep_outdir(config):

    input_yaml = config.input_yaml
    input_file = config.args.input_file
    args       = config.args

    key = 'OUTDIR'
    if key not in input_yaml:
        sys.exit(f"\n ERROR: missing required OUTDIR key in {input_file}\n")

    OUTDIR = input_yaml[key]
    if args.verify : 
        OUTDIR_ORIG = OUTDIR
        OUTDIR     += "_VERIFY"
        input_yaml['OUTDIR']      = OUTDIR
        input_yaml['OUTDIR_ORIG'] = OUTDIR_ORIG

    do_mkdir = False

    if os.path.exists(OUTDIR) :
        if args.clobber : 
            do_mkdir = True
            cmd = f"rm -r {OUTDIR}"
            os.system(cmd)
    else :
        do_mkdir = True

    
    if do_mkdir:  
        print(f" Create OUTDIR  /{OUTDIR}")
        os.mkdir(OUTDIR)
    else:
        print(f" Skip creating existing OUTDIR  /{OUTDIR}")

    # end prep_outdir

def get_survey_info(config):

    # run snana.
    VERSION           = config.input_yaml['VERSION']
    PRIVATE_DATA_PATH = config.input_yaml['PRIVATE_DATA_PATH']

    TEXTFILE_PREFIX   = "TEMP_GET_SURVEY" # prefix for YAML output
    yaml_file         = f"{TEXTFILE_PREFIX}.YAML"
    log_file          = f"{TEXTFILE_PREFIX}.LOG"

    print(f" Extract SURVEY-FILTER info from {VERSION} :")

    cmd = f"{JOBNAME_SNANA} NOFILE " \
          f"VERSION_PHOTOMETRY {VERSION} " \
          f"PRIVATE_DATA_PATH {PRIVATE_DATA_PATH} " \
          f"TEXTFILE_PREFIX {TEXTFILE_PREFIX} " \
          f"MXEVT_PROCESS 0 OPT_YAML 1 > {log_file} "

    os.system(cmd)

    snana_yaml = read_yaml(yaml_file)
    survey  = snana_yaml['SURVEY']
    filters = snana_yaml['FILTERS']
    print(f"\t -> Found SURVEY-FILTERS = {survey}-{filters} ")

    cmd_rm = f"rm {yaml_file} {log_file}"
    os.system(cmd_rm)

    return survey, filters

    # read yaml file to get survey

    # end get_survey_info

def stage_prefix(ISTAGE):
    prefix = f"STAGE{ISTAGE:02d}"
    return prefix

def create_fake_simlib(ISTAGE,config):

    # Run snana.exe job on fakes with option to create SIMLIB
    # where MAG column is true mag.

    OUTDIR   = config.input_yaml['OUTDIR']
    prefix   = stage_prefix(ISTAGE)

    # load name of SIMLIB file here before ISTAGE check
    SIMLIB_OUTFILE = f"{prefix}_FAKES.SIMLIB"
    config.SIMLIB_FILE = SIMLIB_OUTFILE

    print(f"{prefix}: create SIMLIB/cadence file from fakes")
    if ISTAGE < config.args.start_stage :
        print(f"\t Already done --> SKIP")
        return

    KCOR_FILE         = config.input_yaml['KCOR_FILE']
    VERSION           = config.input_yaml['VERSION']
    PRIVATE_DATA_PATH = config.input_yaml['PRIVATE_DATA_PATH']
    OPT_SIMLIB        = config.input_yaml['OPT_SIMLIB']


    nml_lines  = []
    nml_prefix = f"{prefix}_make_simlib"

    # create lines for nml file
    nml_lines.append(f"   PRIVATE_DATA_PATH  = '{PRIVATE_DATA_PATH}' ")
    nml_lines.append(f"   VERSION_PHOTOMETRY = '{VERSION}' ") 
    nml_lines.append(f"   KCOR_FILE          = '{KCOR_FILE}' ")
    nml_lines.append(f"   SNTABLE_LIST   = '' ")
    nml_lines.append(f"   SIMLIB_OUTFILE = '{SIMLIB_OUTFILE}' ")
    nml_lines.append(f"   OPT_SIMLIB_OUT = {OPT_SIMLIB} ")
    nml_lines.append(f"")

    if 'EXTRA_SNLCINP_ARGS' in config.input_yaml:
        for arg in config.input_yaml['EXTRA_SNLCINP_ARGS']:
            nml_lines.append(f"   {arg}")
        
    # - - - - - - 
    run_snana_job(config, nml_prefix, nml_lines)
    sys.stdout.flush()

    # end create_fake_simlib

def run_snana_job(config, nml_prefix, nml_lines):

    args       = config.args
    input_yaml = config.input_yaml
    OUTDIR   = input_yaml['OUTDIR']
    log_file = f"{nml_prefix}.log"
    nml_file = f"{nml_prefix}.nml"
    NML_FILE = f"{OUTDIR}/{nml_file}"

    with open(NML_FILE,"wt") as f:
        f.write(f" &SNLCINP\n")
        
        if args.verify:
            OUTDIR_ORIG = input_yaml['OUTDIR_ORIG']
            orig_file   = f"../{OUTDIR_ORIG}/{FLUXERRMODEL_FILENAME_FAKE}"
            f.write(f"   FLUXERRMODEL_FILE = '{orig_file}' \n")

        for line in nml_lines:
            f.write(f"{line}\n")
        f.write(f" &END\n") 

    print(f"\t Run {JOBNAME_SNANA} on {nml_file} ")
    sys.stdout.flush()

    # run it ...
    cmd = f"cd {OUTDIR}; {JOBNAME_SNANA} {nml_file} > {log_file} "
    os.system(cmd)


    # end run_snana_job

def simgen(ISTAGE,config):

    args           = config.args
    OUTDIR         = config.input_yaml['OUTDIR']
    filters        = config.filters
    SIMLIB_FILE    = config.SIMLIB_FILE
    KCOR_FILE      = config.input_yaml['KCOR_FILE']

    if 'HOSTLIB_FILE' in config.input_yaml :
        HOSTLIB_FILE   = config.input_yaml['HOSTLIB_FILE']
        HOSTLIB_MSKOPT = 258
    else:
        HOSTLIB_FILE   = "NONE"
        HOSTLIB_MSKOPT = 0

    prefix         = stage_prefix(ISTAGE)
    sim_input_file = f"{prefix}_simgen_fakes.input"
    sim_log_file   = f"{prefix}_simgen_fakes.log"

    SIM_INPUT_FILE = f"{OUTDIR}/{sim_input_file}"
    SIM_LOG_FILE   = f"{OUTDIR}/{sim_log_file}"
    GENVERSION     = f"{prefix}_simgen_fakes_{USERNAME4}"
    config.SIM_GENVERSION = GENVERSION

    ranseed = 12345 
    sim_input_lines = []

    print(f"{prefix}: run SNANA simulation using SIMLIB model")
    if ISTAGE < config.args.start_stage :
        print(f"\t Already done --> SKIP")
        return


    if args.verify :
        OUTDIR_ORIG = config.input_yaml['OUTDIR_ORIG']
        orig_file   = f"../{OUTDIR_ORIG}/{FLUXERRMODEL_FILENAME_SIM}"
        sim_input_lines.append(f"FLUXERRMODEL_FILE:  {orig_file}")
        sim_input_lines.append(f" ")

    sim_input_lines.append(f"GENVERSION:        {GENVERSION}")
    sim_input_lines.append(f"SIMLIB_FILE:       {SIMLIB_FILE}")
    sim_input_lines.append(f"SIMLIB_MSKOPT:     4       # stop at end of SIMLIB file")
    sim_input_lines.append(f"NGENTOT_LC:        1000000  # any large number")
    sim_input_lines.append(f"GENSOURCE:         RANDOM")
    sim_input_lines.append(f"GENMODEL:          SIMLIB")
    sim_input_lines.append(f"GENFILTERS:        {filters}")
    sim_input_lines.append(f"KCOR_FILE:         {KCOR_FILE}")    
    sim_input_lines.append(f"HOSTLIB_FILE:      {HOSTLIB_FILE}")
    sim_input_lines.append(f"HOSTLIB_MSKOPT:    {HOSTLIB_MSKOPT}")

    sim_input_lines.append(f"RANSEED:           {ranseed} ")
    sim_input_lines.append(f"FORMAT_MASK:       32  # 2=TEXT  32=FITS ")
    sim_input_lines.append(f"SMEARFLAG_FLUX:    1   # Poisson noise from sky+source")
    sim_input_lines.append(f"SMEARFLAG_ZEROPT:  0")
    sim_input_lines.append(f"OPT_MWEBV:         1  # write MWEBV to data; not applied")
    sim_input_lines.append(f"GENSIGMA_MWEBV_RATIO: 0")
    sim_input_lines.append(f" ")
    sim_input_lines.append(f"USE_SIMLIB_PEAKMJD:   1 ")
    sim_input_lines.append(f"USE_SIMLIB_REDSHIFT:  1 ")
    sim_input_lines.append(f"GENRANGE_PEAKMJD:     40000  80000 ")
    sim_input_lines.append(f"GENRANGE_REDSHIFT:    0.012  1.9 ")
    sim_input_lines.append(f"GENRANGE_TREST:      -100 100 ")

    with open(SIM_INPUT_FILE,"wt") as f:
        for line in sim_input_lines:
            f.write(f"{line}\n")

    print(f"\t Run {JOBNAME_SIM} to generate {GENVERSION} ")
    sys.stdout.flush()

    cmd = f"cd {OUTDIR} ; {JOBNAME_SIM} {sim_input_file} > {sim_log_file}"
    os.system(cmd)

    # check for FATAL error
    f = open(f"{SIM_LOG_FILE}",  "r")
    if 'FATAL' in f.read():
        sys.exit(f"\n FATAL ERROR: check {SIM_LOG_FILE} \n")

    # end simgen

def make_outlier_table(ISTAGE,config,what):

    # run snana.exe with OUTLIER(nsig:0) to create flux table
    # for all observations.
    # Input what = FAKE or SIM

    OUTDIR = config.input_yaml['OUTDIR']
    prefix = stage_prefix(ISTAGE)
    
    nml_prefix   = f"{prefix}_fluxTable_{what}"
    table_file   = f"{nml_prefix}.OUTLIER.TEXT"

    print(f"{prefix}: make {TABLE_NAME} table for {what}")
    sys.stdout.flush()
    if ISTAGE < config.args.start_stage :
        print(f"\t Already done --> SKIP")
        return table_file

    KCOR_FILE         = config.input_yaml['KCOR_FILE']

    if what == STRING_FAKE :
        VERSION           = config.input_yaml['VERSION']
        PRIVATE_DATA_PATH = config.input_yaml['PRIVATE_DATA_PATH']
    else:
        # for SIM
        VERSION = config.SIM_GENVERSION
        PRIVATE_DATA_PATH = ''

    nml_lines       = []

    # create lines for nml file
    nml_lines.append(f"   PRIVATE_DATA_PATH  = '{PRIVATE_DATA_PATH}' ")
    nml_lines.append(f"   VERSION_PHOTOMETRY = '{VERSION}' ") 
    nml_lines.append(f"   KCOR_FILE          = '{KCOR_FILE}' ")
    nml_lines.append(f"   SNTABLE_LIST       = 'SNANA OUTLIER(nsig:0.0)' ")
    nml_lines.append(f"   TEXTFILE_PREFIX    = '{nml_prefix}' ")
    nml_lines.append(f"")

    if 'EXTRA_SNLCINP_ARGS' in config.input_yaml:
        for arg in config.input_yaml['EXTRA_SNLCINP_ARGS']:
            nml_lines.append(f"   {arg}")            

    # - - - - - - 
    run_snana_job(config, nml_prefix, nml_lines)

    
    # compress large TEXT tables
    print(f"\t gzip TEXT tables from {JOBNAME_SNANA} ... ")
    cmd = f"cd {OUTDIR}; gzip STAGE*.TEXT"
    os.system(cmd)

    return table_file 

    #y end make_outlier_table

def parse_map_bins(config):

    # add map_bins dictionary to config
    # Each input row includes; varname  Nbin min max

    input_yaml      = config.input_yaml
    FLUXERRMAP_BINS = input_yaml['FLUXERRMAP_BINS']
    NFIELD_GROUP    = input_yaml['NFIELD_GROUP']

    # if FIELD dependent, insert FIELD as first element in map
    ivar_field = -9 
    if NFIELD_GROUP > 0 :  
        nbin=NFIELD_GROUP;  valmin=-0.5; valmax = nbin - 0.5
        row = f"{COLNAME_IFIELD}  {nbin}  {valmin}  {valmax}"
        FLUXERRMAP_BINS.insert(0,row)
        ivar_field = 0

    # if FILTER var is given, hard-wire nbin and range 
    NDIM = 0 ; ivar_filter = -9
    for row in FLUXERRMAP_BINS:
        NDIM += 1 ;   row=row.split();   varname = row[0]
        if varname == 'FILTER' or varname == 'BAND' :
            nbin = IFILTOBS_MAX; valmin = -0.5; valmax=nbin-0.5
            row = f"{COLNAME_IFILTOBS} {nbin} {valmin} {valmax}"
            FLUXERRMAP_BINS[NDIM-1] = row
            ivar_filter = NDIM - 1

    #print(f" xxx FLUXERRMAP_BINS = {FLUXERRMAP_BINS} ")

    # - - - - - - - - - - - - - - 
    map_bin_dict = {}
    varname_list=[] ;  nbin_list=[] ;  valmin_list=[]; valmax_list=[]
    bin_edge_list=[]

    #sys.exit(f"\n xxx FLUXERRMAP_BINS = \n{FLUXERRMAP_BINS}")

    NDIM   = 0 
    NBIN1D = 1
    for row in FLUXERRMAP_BINS:
        NDIM   += 1
        row     = row.split()        
        varname = row[0]
        nbin=int(row[1]);  valmin=float(row[2]); valmax=float(row[3])
        NBIN1D *= nbin
        bins    = np.linspace(valmin,valmax,nbin+1)
        print(f"    Store {nbin:2d} {varname} bins from {valmin} to {valmax}")
        varname_list.append(varname)
        nbin_list.append(nbin)
        valmin_list.append(valmin)
        valmax_list.append(valmax)
        bin_edge_list.append(bins)

    # make list for header without FIELD or IFILTOBS 
    varname_header_list = varname_list.copy() 
    if COLNAME_IFIELD in varname_header_list:
        varname_header_list.remove(COLNAME_IFIELD)
    if COLNAME_IFILTOBS in varname_header_list :
        varname_header_list.remove(COLNAME_IFILTOBS)

    # - - - - -
    id_1d = np.arange(NBIN1D)  # 0,1,2 ... NBIN1D-1
    id_nd = []                 # array of indices for each dimension

    if NDIM == 1 :
        id0 = np.unravel_index(id_1d,(nbin_list[0]))
        id_nd.append(id0)
        indexing_array = np.arange(NBIN1D).reshape((nbin_list[0]))
    elif NDIM == 2 :
        id0,id1 = np.unravel_index(id_1d,(nbin_list[0],nbin_list[1]))
        id_nd.append(id0)
        id_nd.append(id1)
        indexing_array = np.arange(NBIN1D).reshape((nbin_list[0],nbin_list[1]))
    elif NDIM == 3 :
        id0,id1,id2 = \
            np.unravel_index(id_1d,(nbin_list[0],nbin_list[1],nbin_list[2]))
        id_nd.append(id0)
        id_nd.append(id1)
        id_nd.append(id2)
        indexing_array = \
            np.arange(NBIN1D).reshape((nbin_list[0],nbin_list[1],nbin_list[2]))
    elif NDIM == 4 :
        id0,id1,id2,id3 = \
            np.unravel_index(id_1d,(nbin_list[0],nbin_list[1],nbin_list[2],nbin_list[3]))
        id_nd.append(id0)
        id_nd.append(id1)
        id_nd.append(id2)
        id_nd.append(id3)
        indexing_array = \
            np.arange(NBIN1D).reshape((nbin_list[0],nbin_list[1],nbin_list[2],nbin_list[3]))

    else :
        sys.exit("\n ERROR: cannot process NDIM={NDIM}\n")

    #print(f" xxx nbin_list = {nbin_list} ")
    #print(f" xxx id_nd = \n{id_nd}")
    #print(f" xxx indexing_array=\n{indexing_array} ")
    #print(f" xxx  idem(1,2) = {indexing_array[1,2]} ")

    map_bin_dict = {
        'NDIM'          : NDIM,       # number of map dimensions
        'NBIN1D'        : NBIN1D,      # total number of multiD bins
        'NVAR'          : len(varname_list),
        'varname_list'  : varname_list ,
        'varname_header_list'  : varname_header_list ,
        'nbin_list'     : nbin_list ,
        'valmin_list'   : valmin_list ,
        'valmax_list'   : valmax_list ,
        'bin_edge_list' : bin_edge_list ,   # histogram bin edges
        'id_1d'         : id_1d,
        'id_nd'         : id_nd,
        'indexing_array': indexing_array,
        'ivar_field'    : ivar_field,
        'ivar_filter'   : ivar_filter   # flag to make filter-dependent maps        
    }

    config.map_bin_dict = map_bin_dict
    sys.stdout.flush()

    # end parse_map_bins

def make_fluxerr_model_map(ISTAGE,config):

    #
    # Driver to read OUTLIER tables, compute fluxerr maps,
    # and write maps for SNANA simulation. 
    # Output is one map from fakes (for real data) and
    # one map for SNANA sim.
    #  

    OUTDIR       = config.input_yaml['OUTDIR']
    map_bin_dict = config.map_bin_dict
    prefix       = stage_prefix(ISTAGE)

    fluxerrmodel_file_fake = f"{OUTDIR}/{FLUXERRMODEL_FILENAME_FAKE}"
    fluxerrmodel_file_sim  = f"{OUTDIR}/{FLUXERRMODEL_FILENAME_SIM}"

    print(f"{prefix}: create FLUXERRMODEL maps. ")
    sys.stdout.flush()

    flux_table_fake = f"{OUTDIR}/{config.flux_table_fake}"
    flux_table_sim  = f"{OUTDIR}/{config.flux_table_sim}"
    if not os.path.exists(flux_table_fake):  flux_table_fake += '.gz'
    if not os.path.exists(flux_table_sim):   flux_table_sim  += '.gz'

    # read each table
    df_fake = store_flux_table(flux_table_fake, map_bin_dict)
    df_sim  = store_flux_table(flux_table_sim,  map_bin_dict)

    # load list of unique ifiltobs & band into map_bin_dict.ifiltobs_set, band_set
    get_filter_list(df_fake, map_bin_dict)

    # add index columns, force bounds, apply cuts ...
    df_fake, df_sim = modify_tables(df_fake, df_sim, config)

    # - - - - -
    #print(f"\n xxx df_fake = \n{df_fake[['LOGSNR','PSF', 'i_LOGSNR']]}")
    #print(f"\n xxx df_fake = \n{df_fake}")
    # - - - - - 

    # open output map files
    f_fake = open(fluxerrmodel_file_fake,"wt")
    f_sim  = open(fluxerrmodel_file_sim, "wt")

    write_map_global_header(f_fake, STRING_FAKE, config)
    write_map_global_header(f_sim,  STRING_SIM,  config)

    # get a few things for 1D loop over bins
    NBIN1D        = map_bin_dict['NBIN1D']
    id_nd         = map_bin_dict['id_nd'] 
    ivar_filter   = map_bin_dict['ivar_filter']
    ivar_field    = map_bin_dict['ivar_field']
    ifiltobs_list = map_bin_dict['ifiltobs_list']
    ifiltobs_last = -9
    ifield_last   = -9

    # if there is no filter dependence, write one header 
    # with BAND specifying all bands.
    if ivar_filter < 0 :
        write_map_header(f_fake, -9, config)
        write_map_header(f_sim,  -9, config)

    # start loop over 1D bins (which loops over all dimensions of map)

    print(f" Begin loop over {NBIN1D} 1D map bins ... ")
    sys.stdout.flush()

    for BIN1D in range(0,NBIN1D):

        ifield = -9
        if ivar_field >= 0: ifield = id_nd[ivar_field][BIN1D]

        # check for start of filter-dependent map
        use_filter = True
        if ivar_filter >= 0:
            ifiltobs   = id_nd[ivar_filter][BIN1D]
            use_filter = ifiltobs in ifiltobs_list
            if use_filter and ifiltobs != ifiltobs_last :
                write_map_header(f_fake, ifield, ifiltobs, config)
                write_map_header(f_sim,  ifield, ifiltobs, config)
                
            ifiltobs_last = ifiltobs

        if not use_filter : continue  # skip the pad zeros in ifiltobs_list

        # select sample in this BIN1D (this multi-D cell)
        pull_fake = \
            df_fake['PULL'].to_numpy()[np.where(df_fake[COLNAME_BIN1D]==BIN1D)]
        pull_sim = \
            df_sim['PULL'].to_numpy()[np.where(df_sim[COLNAME_BIN1D]==BIN1D)]
        ratio_fake = \
            df_fake['ERR_RATIO'].to_numpy()[np.where(df_fake[COLNAME_BIN1D]==BIN1D)]

        # compute errScale correction for fake and sim
        n_fake, n_sim, cor_fake, cor_sim = \
            compute_errscale_cor ( pull_fake, pull_sim, ratio_fake )

        # update map files.
        write_map_row(f_fake, config, BIN1D, cor_fake, n_fake, -9)
        write_map_row(f_sim,  config, BIN1D, cor_sim,  n_fake, n_sim )

    # - - - 
    print("\n")
    print(f" Done creating {fluxerrmodel_file_fake} ")
    print(f" Done creating {fluxerrmodel_file_sim} ")
    sys.stdout.flush()

    return
    # end make_fluxerr_model_map


def  modify_tables(df_fake, df_sim, config):

    # Modify tables by
    #  + applying cuts to reject some rows
    #  + force map variable values to lie within map bounds
    #  + add bin-index columns


    map_bin_dict   = config.map_bin_dict
    args           = config.args
    input_yaml     = config.input_yaml

    NDIM           = map_bin_dict['NDIM']
    NBIN1D         = map_bin_dict['NBIN1D']
    varname_list   = map_bin_dict['varname_list']
    nbin_list      = map_bin_dict['nbin_list']
    valmin_list    = map_bin_dict['valmin_list']
    valmax_list    = map_bin_dict['valmax_list']
    bin_edge_list  = map_bin_dict['bin_edge_list']
    id_1d          = map_bin_dict['id_1d'] 
    id_nd          = map_bin_dict['id_nd'] 
    ivar_filter    = map_bin_dict['ivar_filter']

    # apply optional cuts on NSIG and ERR_RATIO

    nrow_orig_fake = len(df_fake)
    nrow_orig_sim  = len(df_sim)

    key = 'CUTWIN_NSIG'
    if key in input_yaml :
        cutwin_nsig = input_yaml[key].split()
        nsig_max    = float(cutwin_nsig[1])
        df_fake     = df_fake.loc[ df_fake['NSIG'] < nsig_max ]
        df_sim      = df_sim.loc[  df_sim['NSIG']  < nsig_max ]

        # reset indices
        df_fake     = df_fake.reset_index()
        df_sim      = df_sim.reset_index()

    #sys.exit(f"\n xxx df_fake = \n{df_fake}\n")

    nrow_fake   = len(df_fake)
    nrow_sim    = len(df_sim)
    config.nrow_fake  = nrow_fake
    config.nrow_sim   = nrow_sim

    print(f" Nrow(fake) = {nrow_orig_fake} -> {nrow_fake} after cuts.")
    print(f" Nrow(sim)  = {nrow_orig_sim} -> {nrow_sim} after cuts.")
    sys.stdout.flush()

    # - - - - 
    # force variables in map to lie within map ranges e.g., 
    # if LOGSNR has 5 bins from 0.3 to 2.3, then LOGSNR<0.3 -> 0.30001
    force_bounds(df_fake, config)
    force_bounds(df_sim,  config)

    # assign integer IDFIELD = 0, 1, 2, ... NFIELD_GROUP-1 to each row
    NFIELD_GROUP = input_yaml['NFIELD_GROUP']
    if NFIELD_GROUP > 0 :
        FIELD_LISTS = input_yaml['FIELD_GROUP_LISTS']
        print(f"   Add {COLNAME_IFIELD} column to tables ...")
        df_fake[COLNAME_IFIELD] = \
            df_fake.apply(lambda row: apply_field(row,FIELD_LISTS), axis=1)

        df_sim[COLNAME_IFIELD] = \
            df_sim.apply(lambda row: apply_field(row,FIELD_LISTS), axis=1)
        
    #sys.exit(f"\n xxx BYE BYE df_fake=\n{df_fake}\n")

    # - - - - - 
    # digitize the variables used in map; i.e., compute multi-D
    # index for each map-variable and each row in table.
    ivar=0
    ibins_fake = [ ];   ibins_sim = [ ]  # init arrays of multi-D indices
    for varname, bins in zip(varname_list, bin_edge_list):
        dfcol_fake = df_fake[varname]
        dfcol_sim  = df_sim[varname]
        ibins_fake.append(np.digitize(dfcol_fake, bins))
        ibins_sim.append(np.digitize( dfcol_sim,  bins))
        ivarname = f"i_{varname}"  
        df_fake[ivarname] = ibins_fake[ivar] - 1
        df_sim[ivarname]  = ibins_sim[ivar]  - 1
        ivar += 1

    # add 1d index colum to each flux table to enable easy selection
    # of multi-D cells from 1D index.
    print(f"   Add {COLNAME_BIN1D} column to tables ...")
    df_fake[COLNAME_BIN1D] = \
        df_fake.apply(lambda row: apply_id_1d(row,map_bin_dict), axis=1)

    df_sim[COLNAME_BIN1D] = \
        df_sim.apply(lambda row: apply_id_1d(row,map_bin_dict), axis=1)

    return df_fake, df_sim

    # end modify_tables

def force_bounds(df,config):

    map_bin_dict = config.map_bin_dict
    varname_list = map_bin_dict['varname_list']
    valmin_list  = map_bin_dict['valmin_list']
    valmax_list  = map_bin_dict['valmax_list']

    for varname,valmin,valmax in zip(varname_list,valmin_list,valmax_list):
        if varname == COLNAME_IFILTOBS : continue
        if varname == COLNAME_IFIELD   : continue
        print(f"\t Force {valmin} < {varname} < {valmax}")
        df[varname] = df[varname].where(df[varname] > valmin, valmin+0.0001)
        df[varname] = df[varname].where(df[varname] < valmax, valmax-0.0001)
        
    sys.stdout.flush()
    # end force_bounds


def compute_errscale_cor(pull_fake, pull_sim, ratio_fake):
    
    # for this 1D bin, compute ERRSCALE correction for FAKE(data) and SIM.
    #  + pull_fake a list of PULL = (F-Ftrue)/ERR_CALC
    #  + pull_sim  is the same for sime
    #  + ratio_fake is list of ERR_DATA/ERR_CALC [fakes]
    #
    #  From  Sec 6.4 of https://arxiv.org/pdf/1811.02379.pdf 
    #
    #  Eq 15 for FAKE (intended for data)
    #     scale = RMS[(F-Ftrue)/ERRCALC]_fake / <ERR_F/ERR_CALC>_fake
    #
    #  Eq 14 for SIM (intended for sim)
    #     scale = RMS[(F-Ftrue)/ERRCALC]_fake / RMS[(F-Ftrue)/ERRCALC]_sim

    n_fake = len(pull_fake)
    n_sim  = len(pull_sim)
    if n_fake > 5 and n_sim > 5 :

        # shift pulls so that median/avg is zero
        avg_pull_fake  = np.median(pull_fake)
        avg_pull_sim   = np.median(pull_sim)
        pull_fake      = pull_fake - avg_pull_fake
        pull_sim       = pull_sim  - avg_pull_sim

        # for RMS, compute 1.48*median|pull| to reduce sensitivity to outliers
        rms_pull_fake  = 1.48 * np.median(np.absolute(pull_fake))
        rms_pull_sim   = 1.48 * np.median(np.absolute(pull_sim))

        avg_ratio      = np.median(ratio_fake)  # ERR_DATA/ERR_CALC

        # finally, the map corrections
        cor_fake       = rms_pull_fake / avg_ratio     # correct fake & data
        cor_sim        = rms_pull_fake / rms_pull_sim  # correct sims
        
        #print(f"\t xxx cor_sim = {rms_pull_fake:.3f} / {rms_pull_sim:.3f}" \
        #      f" = {cor_fake:.3f}  " \
        #      f" (avgPull={avg_pull_fake:.3f},{avg_pull_sim:0.3f}) " )

    else:
        cor_fake = 1.0 ; cor_sim = 1.0

    return n_fake, n_sim, cor_fake, cor_sim

    # end compute_errscale_cor

def write_map_global_header(f, what, config):
    
    # write global comments and one-time keys.
    # what = FAKE or SIM
    # If map depends on fields, define each field group.

    input_yaml        = config.input_yaml
    NFIELD_GROUP      = input_yaml['NFIELD_GROUP']
    FIELD_GROUP_NAMES = input_yaml['FIELD_GROUP_NAMES']
    FIELD_GROUP_LISTS = input_yaml['FIELD_GROUP_LISTS']

    map_bin_dict      = config.map_bin_dict
    NDIM              = map_bin_dict['NDIM']
    varname_list      = map_bin_dict['varname_list']

    nrow_fake = config.nrow_fake
    nrow_sim  = config.nrow_sim

    # for filter and field, replace internal index names with
    # more reasonable FIELD and BAND
    varname_string    = ' '.join(varname_list)
    varname_string    = varname_string.replace(COLNAME_IFIELD,"FIELD")
    varname_string    = varname_string.replace(COLNAME_IFILTOBS,"BAND")

    if what == STRING_SIM:
        usage_code  = "snana.exe and snlc_sim.exe" ; 
        usage_key   = "FLUXERRMODEL_FILE"
        item        = what
        string_nrow = f"Nobs(FAKE,SIM) = {nrow_fake}, {nrow_sim}"
    else:
        usage_code  = "snlc_fit.exe" 
        usage_key   = "FLUXERRMODEL_FILE in &SNLCINP"
        item        = f"FAKES and DATA"
        string_nrow = f"Nobs(FAKE) = {nrow_fake} "

    # - - - 
    f.write(f"DOCUMENTATION:")
    f.write(f"""
  PURPOSE: correct flux uncertainty for {item}
  REF:
  - AUTHOR: Kessler et al, 2019 (DES3YR sims, see Sec 6.4)
    ADS:    https://ui.adsabs.harvard.edu/abs/2019MNRAS.485.1171K
  INTENT:  Test
  USAGE_CODE:  {usage_code}
  USAGE_KEY:   {usage_key}
  NOTES:
  - map dependence is {varname_string}
  - {string_nrow}
  - map-create command =  {sys.argv[0]} {sys.argv[1]} 
  - created by user={USERNAME} on HOST={HOSTNAME}  
    """)
    f.write(f"\nDOCUMENTATION_END:\n")
    f.write(f"\n\n")

    for field_name, field_list in zip(FIELD_GROUP_NAMES,FIELD_GROUP_LISTS):
        snana_field_list = '+'.join(field_list)
        f.write(f"DEFINE_FIELDGROUP: {field_name}  {snana_field_list}\n")

    f.flush()
    # end write_map_define_fields

def write_map_header(f, ifield, ifiltobs, config):

    map_bin_dict  = config.map_bin_dict
    input_yaml    = config.input_yaml

    FIELD_GROUP_NAMES   = input_yaml['FIELD_GROUP_NAMES']
    band_map            = map_bin_dict['band_map']
    band_list           = map_bin_dict['band_list']
    varname_header_list = map_bin_dict['varname_header_list']
    varlist             = ' '.join(varname_header_list)

    if ifield >=0 : 
        field_arg = FIELD_GROUP_NAMES[ifield]

    if ifiltobs < 0 :
        band_arg = ''.join(band_list)
    else:
        band_arg = band_map[ifiltobs]

    map_name = "FLUXERR_SCALE"
    f.write("\n")
    f.write(f"MAPNAME: {map_name} \n")
    if ifield >= 0 : f.write(f"FIELD: {field_arg} \n")
    f.write(f"BAND: {band_arg} \n")
    f.write(f"VARNAMES: {varlist}   ERRSCALE\n")

    f.flush()
    # end write_map_header

def write_map_row(f, config, BIN1D, cor, n_fake, n_sim ):

    # write map row corresponding to 1D index BIN1D
    # cor is the ERRSCALE correction 

    map_bin_dict    = config.map_bin_dict
    bin_edge_list   = map_bin_dict['bin_edge_list']
    NVAR            = map_bin_dict['NVAR']
    id_nd           = map_bin_dict['id_nd']
    varname_list    = map_bin_dict['varname_list']
    ivar_field      = map_bin_dict['ivar_field']
    ivar_filter     = map_bin_dict['ivar_filter']
    nbin_list       = map_bin_dict['nbin_list']

    #print(f" bin_list = {bin_list}")
    # convert to multi-D indices and extract grid values
    row_line = 'ROW: '
    last_row = True

    for ivar in range(0,NVAR):
        if ivar == ivar_field  : continue
        if ivar == ivar_filter : continue
        itmp       = id_nd[ivar][BIN1D]
        lo_edge    = bin_edge_list[ivar][itmp]
        hi_edge    = bin_edge_list[ivar][itmp+1]
        bin_center = 0.5*(lo_edge + hi_edge)
        row_line  += f"{bin_center:8.4f}  "
        if itmp < nbin_list[ivar]-1 : last_row = False

    row_line += f"{cor:8.3f}"

    # prepare comment with stats
    if n_sim > 0:
        comment = f"N_[FAKE,SIM] = {n_fake} , {n_sim}"
    else :
        comment = f"N_FAKE = {n_fake} "

    f.write(f"{row_line}    # {comment} \n")

    if last_row:   
        f.write("ENDMAP:\n\n")
        f.flush()

    # end write_map_row

def store_flux_table(flux_table, map_bin_dict):


    df = pd.read_csv(flux_table, comment="#", delim_whitespace=True)

    nrow = len(df)
    print(f"    Read/store {flux_table} with {nrow} rows.")

    STR_F        = 'FLUXCAL_DATA' ; 
    STR_FTRUE    = 'FLUXCAL_TRUE' ; 
    STR_ERR      = 'FLUXCAL_ERR_DATA'
    STR_ERR_CALC = 'FLUXCAL_ERR_CALC'

    # compute modified PULL with ERR -> ERR_CALC
    pull = (df[STR_F]-df[STR_FTRUE])/df[STR_ERR_CALC] 
    df['PULL'] = pull.values

    err_ratio  = df[STR_ERR] / df[STR_ERR_CALC]
    df['ERR_RATIO'] = err_ratio.values

    return df

    # end store_flux_table

def get_filter_list(df, map_bin_dict):

    # get unique list of IFILTOBS and BAND 

    ifiltobs_list = sorted(list(set(df[COLNAME_IFILTOBS])))
    band_list     = []
    band_map      = [ -9 ]*100

    for ifiltobs in ifiltobs_list :
        indx = df[df[COLNAME_IFILTOBS] == ifiltobs].index[0]
        band = df[COLNAME_BAND][indx]
        band_list.append(band)
        band_map[ifiltobs] = band

    band_string = ''.join(band_list)
    print(f"    Found IFILTOBS set = {ifiltobs_list}")
    print(f"       -->    BAND set = {band_list}  ({band_string})")

    map_bin_dict['ifiltobs_list']  = ifiltobs_list
    map_bin_dict['band_list']      = band_list
    map_bin_dict['band_map']       = band_map
    map_bin_dict['band_string']    = band_string
    return
    # end get_filter_list


def apply_field(row,FIELD_LISTS):

    # return IFIELD index for this row.
    # Input FIELD_LISTS is a list of lists; e..g,
    # [ ['X3','C3'] , ['S1', 'S2', 'X1', 'X2'] ]

    FIELD = row['FIELD']
    ifield = 0
    for field_list in FIELD_LISTS:
        if FIELD in field_list: return ifield
        ifield += 1

    # if we get here, abort
    sys.exit(f"\n ERROR: FIELD={FIELD} is not in \n\t {FIELD_LISTS}. " \
             f"\n\t See FIELDS arg in input file.")

    return ifield
    # end apply_field

def apply_id_1d(row, map_bin_dict):

    # return 1D index for this row.
    
    id_1d          = map_bin_dict['id_1d']
    NDIM           = map_bin_dict['NDIM']
    varname_list   = map_bin_dict['varname_list']
    nbin_list      = map_bin_dict['nbin_list']
    indexing_array = map_bin_dict['indexing_array']
    ib_list       = []

    for varname, nbin in zip(varname_list, nbin_list) : 
        ivarname = f"i_{varname}" 
        # xxx ib       = getbin_varname(row[ivarname], nbin)
        ib       = row[ivarname]
        ib_list.append(ib)

    # - - - 
    # beware: this NDIM logic is goofy & fragile :(

    SMART_WAY = False

    if SMART_WAY:
        # couldn't get this to work; need somebody even smarter ??
        id_1d = indexing_array[np.ix_([[ib_list[i]] for i in ib_list])]
    else:
        # dumb way with NDIM if-block
        if NDIM == 1:
            id_1d  = indexing_array[ib_list[0]]
        elif NDIM == 2 :
            id_1d  = indexing_array[ib_list[0],ib_list[1]]
        elif NDIM == 3 :
            id_1d  = indexing_array[ib_list[0],ib_list[1],ib_list[2]]
        elif NDIM == 4 :
            id_1d = indexing_array[ib_list[0],ib_list[1],ib_list[2],ib_list[3]]
    return id_1d
    # end apply_id_1d

def getbin_varname(ibin_raw, nbin):
    # xxx obsolete xxxx
    ibin = ibin_raw
    if ibin < 0: ibin = 0
    if ibin >= nbin : ibin = nbin-1
    return ibin

# =====================================
#
#      MAIN
#
# =====================================

if __name__ == "__main__":

    config      = Namespace()
    config.args = get_args()

    # option for long HELP menus
    if config.args.HELP :
        see_me = (f" !!! ************************************************ !!!")
        print(f"\n{see_me}\n{see_me}\n{see_me}")
        print(f"{HELP_CONFIG}")
        sys.exit(' Scroll up to see full HELP menu.\n Done: exiting Main.')

    config.input_yaml = read_input(config.args.input_file)

    prep_outdir(config)

    # run snana job to extract name of SURVEY from fake data
    config.survey, config.filters = get_survey_info(config)

    sys.stdout.flush()

    ISTAGE = 0
    print()

    # create simlib from fakes
    ISTAGE += 1
    create_fake_simlib(ISTAGE,config)

    # simulate fakes with snana
    ISTAGE += 1
    simgen(ISTAGE,config)

    # run snana on fakes and sim; create OUTLIER table with nsig>=0
    # to catch all flux observations
    ISTAGE += 1
    config.flux_table_fake = make_outlier_table(ISTAGE,config,STRING_FAKE)
    config.flux_table_sim  = make_outlier_table(ISTAGE,config,STRING_SIM)

    ISTAGE += 1
    parse_map_bins(config)
    make_fluxerr_model_map(ISTAGE,config)

# === END ===

