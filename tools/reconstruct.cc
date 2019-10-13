/*
 * SVRTK : SVR reconstruction based on MIRTK
 *
 * Copyright 2008-2017 Imperial College London
 * Copyright 2018-2019 King's College London
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include "mirtk/Common.h"
#include "mirtk/Options.h"
#include "mirtk/NumericsConfig.h"
#include "mirtk/IOConfig.h"
#include "mirtk/TransformationConfig.h"
#include "mirtk/RegistrationConfig.h"

#include "mirtk/GenericImage.h"
#include "mirtk/GenericRegistrationFilter.h"
#include "mirtk/Transformation.h"
#include "mirtk/HomogeneousTransformation.h"
#include "mirtk/RigidTransformation.h"
#include "mirtk/ImageReader.h"


#include "mirtk/Reconstruction.h"

#include <iostream>
#include <chrono>
#include <ctime>
#include <fstream>
#include <cmath>
#include <set>
#include <algorithm>
#include <thread>
#include <functional>
#include <vector>
#include <cstdlib>
#include <pthread.h>
#include <string>



using namespace mirtk;
using namespace std;

// =============================================================================
//
// =============================================================================

// -----------------------------------------------------------------------------

void usage()
{
    cout << "Usage: reconstruct [reconstructed] [N] [stack_1] .. [stack_N] <options>\n" << endl;
    cout << endl;
    
    cout << "\t[reconstructed]         Name for the reconstructed volume. Nifti or Analyze format." << endl;
    cout << "\t[N]                     Number of stacks." << endl;
    cout << "\t[stack_1] .. [stack_N]  The input stacks. Nifti or Analyze format." << endl;
    cout << "\t" << endl;
    cout << "Options:" << endl;
    cout << "\t-dofin [dof_1]   .. [dof_N]    The transformations of the input stack to template" << endl;
    cout << "\t                        in \'dof\' format used in IRTK." <<endl;
    cout << "\t                        Only rough alignment with correct orienation and " << endl;
    cout << "\t                        some overlap is needed." << endl;
    cout << "\t                        Use \'id\' for an identity transformation for at least" << endl;
    cout << "\t                        one stack. The first stack with \'id\' transformation" << endl;
    cout << "\t                        will be resampled as template." << endl;
    cout << "\t-template [volume]        Template for registration" << endl;
    cout << "\t-thickness [th_1] .. [th_N] Give slice thickness.[Default: twice voxel size in z direction]"<<endl;
    cout << "\t-mask [mask]              Binary mask to define the region od interest. [Default: whole image]"<<endl;
    cout << "\t-packages [num_1] .. [num_N] Give number of packages used during acquisition for each stack."<<endl;
    cout << "\t                          The stacks will be split into packages during registration iteration 1"<<endl;
    cout << "\t                          and then into odd and even slices within each package during "<<endl;
    cout << "\t                          registration iteration 2. The method will then continue with slice to"<<endl;
    cout << "\t                          volume approach. [Default: slice to volume registration only]"<<endl;
    cout << "\t-template_number          Number of the template stack. [Default: 0]"<<endl;
    cout << "\t-iterations [iter]        Number of registration-reconstruction iterations. [Default: 3]"<<endl;
    cout << "\t-sigma [sigma]            Stdev for bias field. [Default: 12mm]"<<endl;
    cout << "\t-resolution [res]         Isotropic resolution of the volume. [Default: 0.75mm]"<<endl;
    cout << "\t-multires [levels]        Multiresolution smooting with given number of levels. [Default: 3]"<<endl;
    cout << "\t-average [average]        Average intensity value for stacks [Default: 700]"<<endl;
    cout << "\t-delta [delta]            Parameter to define what is an edge. [Default: 150]"<<endl;
    cout << "\t-lambda [lambda]          Smoothing parameter. [Default: 0.02]"<<endl;
    cout << "\t-lastIter [lambda]        Smoothing parameter for last iteration. [Default: 0.01]"<<endl;
    cout << "\t-smooth_mask [sigma]      Smooth the mask to reduce artefacts of manual segmentation. [Default: 4mm]"<<endl;
    cout << "\t-global_bias_correction   Correct the bias in reconstructed image against previous estimation."<<endl;
    cout << "\t-no_intensity_matching    Switch off intensity matching."<<endl;
    cout << "\t-no_robust_statistics     Switch off robust statistics."<<endl;
    cout << "\t-no_robust_statistics     Switch off robust statistics."<<endl;
    cout << "\t-svr_only                 Only SVR registration to a template stack."<<endl;
    cout << "\t-ncc                      Use global NCC similarity for SVR steps. [Default: NMI]"<<endl;
    cout << "\t-nmi_bins [nmi_bins]      Number of NMI bins for registration. [Default: 16]"<<endl;
    cout << "\t-structural               Use structrural exclusion of slices at the last iteration."<<endl;
    cout << "\t-exclude_slices_only      Robust statistics for exclusion of slices only."<<endl;
    cout << "\t-template [template]      Use template for initialisation of registration loop. [Default: average of stack registration]"<<endl;
    cerr << "\t-remove_black_background  Create mask from black background."<<endl;
    cerr << "\t-transformations [folder] Use existing slice-to-volume transformations to initialize the reconstruction->"<<endl;
    cerr << "\t-force_exclude [number of slices] [ind1] ... [indN]  Force exclusion of slices with these indices."<<endl;
    cout << "\t-remote                   Run SVR registration as remote functions in case of memory issues [Default: false]."<<endl;
    cout << "\t-debug                    Debug mode - save intermediate results."<<endl;
    cout << "\t" << endl;
    cout << "\t" << endl;
    
    exit(1);
}




// -----------------------------------------------------------------------------

// =============================================================================
// Main function
// =============================================================================

// -----------------------------------------------------------------------------

int main(int argc, char **argv)
{
    const char *current_mirtk_path = argv[0];
    
    UniquePtr<ImageReader> image_reader;
    InitializeIOLibrary();
    
    auto start = std::chrono::system_clock::now();
    auto end = std::chrono::system_clock::now();
    
    auto start_total = std::chrono::system_clock::now();
    auto end_total = std::chrono::system_clock::now();
    
    std::chrono::duration<double> elapsed_seconds;
    std::time_t end_time;
    
    //utility variables
    int i, j, x, y, z, ok;
    char buffer[256];
    RealImage stack;
    RealImage maskedTemplate;
    
    //declare variables for input
    /// Name for output volume
    char * output_name = NULL;
    /// Slice stacks
    Array<RealImage> stacks;
    Array<string> stack_files;
    /// Stack transformation
    Array<RigidTransformation> stack_transformations;
    /// user defined transformations
    bool have_stack_transformations = false;
    /// Stack thickness
    Array<double> thickness;
    ///number of stacks
    int nStacks;
    /// number of packages for each stack
    Array<int> packages;
    
    
    bool use_template = false;
    RealImage template_stack;
    
    
    // Default values.
    int templateNumber=0;
    RealImage *mask=NULL;
    int iterations = 3;
    bool debug = false;
    double sigma = 20;
    double resolution = 0.75;
    double lambda = 0.02;
    double delta = 150;
    int levels = 3;
    double lastIterLambda = 0.01;
    int rec_iterations;
    double averageValue = 700;
    double smooth_mask = 4;
    bool global_bias_correction = false;
    double low_intensity_cutoff = 0.01;
    //folder for slice-to-volume registrations, if given
    char * folder=NULL;
    //flag to remove black background, e.g. when neonatal motion correction is performed
    bool remove_black_background = false;
    //flag to switch the intensity matching on and off
    bool intensity_matching = true;
    bool rescale_stacks = false;
    bool registration_flag = true;
    
    //flags to switch the robust statistics on and off
    bool robust_statistics = true;
    bool robust_slices_only = false;
    
    //flag to replace super-resolution reconstruction by multilevel B-spline interpolation
    bool bspline = false;
    
    RealImage average;
    
    string info_filename = "slice_info.tsv";
    string log_id;
    bool no_log = false;
    
    //forced exclusion of slices
    int number_of_force_excluded_slices = 0;
    vector<int> force_excluded;
    
    //flag for SVR registration to a template (without packages)
    bool svr_only = false;
    
    //flag for struture-based exclusion of slices
    bool structural = false;
    
    //flag for switching off NMI and using NCC for SVR step
    bool ncc_reg_flag = false;
    
    bool remote_flag = false;
    
    //Create reconstruction object
    Reconstruction *reconstruction = new Reconstruction();
    
    cout << "------------------------------------------------------" << endl;
    
    //if not enough arguments print help
    if (argc < 5)
        usage();
    
    //read output name
    output_name = argv[1];
    argc--;
    argv++;
    cout<<"Recontructed volume name : "<<output_name<<endl;
    
    //read number of stacks
    nStacks = atoi(argv[1]);
    argc--;
    argv++;
    cout<<"Number of stacks : "<<nStacks<<endl;
    
    // Read stacks
    
    const char *tmp_fname;
    UniquePtr<BaseImage> tmp_image;
    
    
    
    for (i=0;i<nStacks;i++) {
        
        stack_files.push_back(argv[1]);
        
        cout<<"Stack " << i << " : "<<argv[1]<<endl;
        
        tmp_fname = argv[1];
        image_reader.reset(ImageReader::TryNew(tmp_fname));
        tmp_image.reset(image_reader->Run());
        
        stack = *tmp_image;
        
        double smin, smax;
        stack.GetMinMax(&smin, &smax);
        
        if (smin < 0 || smax < 0) {
            template_stack.PutMinMaxAsDouble(0, 1000);
        }
        
        argc--;
        argv++;
        stacks.push_back(stack);
    }
    
    
    template_stack = stacks[0];
    
    
    // Parse options.
    while (argc > 1) {
        ok = false;
        
        //Read stack transformations
        if ((ok == false) && (strcmp(argv[1], "-dofin") == 0)){
            argc--;
            argv++;
            
            for (i=0;i<nStacks;i++) {
                
                cout<<"Transformation " << i << " : "<<argv[1]<<endl;
                Transformation *t = Transformation::New(argv[1]);
                RigidTransformation *rigidTransf = dynamic_cast<RigidTransformation*> (t);
                
                stack_transformations.push_back(*rigidTransf);
                delete rigidTransf;
                
                argc--;
                argv++;
            }
            reconstruction->InvertStackTransformations(stack_transformations);
            have_stack_transformations = true;
        }
        
        //Read slice thickness
        if ((ok == false) && (strcmp(argv[1], "-thickness") == 0)) {
            argc--;
            argv++;
            cout<< "Slice thickness : ";
            for (i=0;i<nStacks;i++) {
                thickness.push_back(atof(argv[1]));
                cout<<thickness[i]<<" ";
                argc--;
                argv++;
            }
            cout<<endl;
            ok = true;
        }
        
        //Read number of packages for each stack
        if ((ok == false) && (strcmp(argv[1], "-packages") == 0)) {
            argc--;
            argv++;
            cout<< "Package number : ";
            for (i=0;i<nStacks;i++) {
                packages.push_back(atoi(argv[1]));
                cout<<packages[i]<<" ";
                argc--;
                argv++;
            }
            cout<<endl;
            
            reconstruction->SetNPackages(packages);

            ok = true;
        }
        
        //Read template for initialiation of registration
        if ((ok == false) && (strcmp(argv[1], "-template") == 0)) {
            argc--;
            argv++;
            
            cout<<"Template : "<<argv[1]<<endl;
            
            template_stack.Read(argv[1]);
            
            double smin, smax;
            template_stack.GetMinMax(&smin, &smax);
            
            if (smin < 0 || smax < 0) {
                
                template_stack.PutMinMaxAsDouble(0, 1000);
            }
            
            use_template = true;
            reconstruction->SetTemplateFlag(use_template);
            
            ok = true;
            argc--;
            argv++;
        }
        
        //Read binary mask for final volume
        if ((ok == false) && (strcmp(argv[1], "-mask") == 0)) {
            argc--;
            argv++;
            
            cout<<"Mask : "<<argv[1]<<endl;
            
            mask= new RealImage(argv[1]);
            ok = true;
            argc--;
            argv++;
        }
        
        
        
        //Read number of registration-reconstruction iterations
        if ((ok == false) && (strcmp(argv[1], "-iterations") == 0)) {
            argc--;
            argv++;
            iterations=atoi(argv[1]);
            ok = true;
            argc--;
            argv++;
        }
        
        
        //Read template number
        if ((ok == false) && (strcmp(argv[1], "-template_number") == 0)) {
            argc--;
            argv++;
            templateNumber=atoi(argv[1]);
            
            template_stack = stacks[templateNumber];
            
            ok = true;
            argc--;
            argv++;
        }
        
        
        
        //Read number of threads
        if ((ok == false) && (strcmp(argv[1], "-nthreads") == 0)) {
            argc--;
            argv++;
            int n_treads=atoi(argv[1]);
            reconstruction->SetNThreads(n_treads);
            ok = true;
            argc--;
            argv++;
        }
        
        
        //Variance of Gaussian kernel to smooth the bias field.
        if ((ok == false) && (strcmp(argv[1], "-sigma") == 0)) {
            argc--;
            argv++;
            sigma=atof(argv[1]);
            ok = true;
            argc--;
            argv++;
        }
        
        //Smoothing parameter
        if ((ok == false) && (strcmp(argv[1], "-lambda") == 0)) {
            argc--;
            argv++;
            lambda=atof(argv[1]);
            ok = true;
            argc--;
            argv++;
        }
        
        //Smoothing parameter for last iteration
        if ((ok == false) && (strcmp(argv[1], "-lastIter") == 0)) {
            argc--;
            argv++;
            lastIterLambda=atof(argv[1]);
            ok = true;
            argc--;
            argv++;
        }
        
        //Parameter to define what is an edge
        if ((ok == false) && (strcmp(argv[1], "-delta") == 0)) {
            argc--;
            argv++;
            delta=atof(argv[1]);
            ok = true;
            argc--;
            argv++;
        }
        
        //Isotropic resolution for the reconstructed volume
        if ((ok == false) && (strcmp(argv[1], "-resolution") == 0)) {
            argc--;
            argv++;
            resolution=atof(argv[1]);
            ok = true;
            argc--;
            argv++;
        }
        
        //Number of resolution levels
        if ((ok == false) && (strcmp(argv[1], "-multires") == 0)) {
            argc--;
            argv++;
            levels=atoi(argv[1]);
            argc--;
            argv++;
            ok = true;
        }
        
        //SVR reconstruction as remote functions
        if ((ok == false) && (strcmp(argv[1], "-remote") == 0)) {
            argc--;
            argv++;
            remote_flag=true;
            ok = true;
        }
        
        
        //Use only SVR to a template
        if ((ok == false) && (strcmp(argv[1], "-svr_only") == 0)) {
            argc--;
            argv++;
            svr_only=true;
            ok = true;
        }
        

        //Use NCC similarity metric for SVR
        if ((ok == false) && (strcmp(argv[1], "-ncc") == 0)) {
            argc--;
            argv++;
            ncc_reg_flag=true;
            reconstruction->SetNCC(ncc_reg_flag);
            ok = true;
        }
        
        
        //Read transformations from this folder
        if ((ok == false) && (strcmp(argv[1], "-transformations") == 0)){
            argc--;
            argv++;
            folder=argv[1];
            ok = true;
            argc--;
            argv++;
        }
        
        //Remove black background
        if ((ok == false) && (strcmp(argv[1], "-remove_black_background") == 0)){
            argc--;
            argv++;
            remove_black_background=true;
            ok = true;
        }
        
        //Force removal of certain slices
        if ((ok == false) && (strcmp(argv[1], "-force_exclude") == 0)){
            argc--;
            argv++;
            number_of_force_excluded_slices = atoi(argv[1]);
            argc--;
            argv++;
            
            cout<< number_of_force_excluded_slices<< " force excluded slices: ";
            for (i=0;i<number_of_force_excluded_slices;i++)
            {
                force_excluded.push_back(atoi(argv[1]));
                cout<<force_excluded[i]<<" ";
                argc--;
                argv++;
            }
            cout<<"."<<endl;
            
            ok = true;
        }
        
        //Switch off intensity matching
        if ((ok == false) && (strcmp(argv[1], "-no_intensity_matching") == 0)) {
            argc--;
            argv++;
            intensity_matching=false;
            ok = true;
        }
        
        //Switch off robust statistics
        if ((ok == false) && (strcmp(argv[1], "-no_robust_statistics") == 0)) {
            argc--;
            argv++;
            robust_statistics=false;
            ok = true;
        }

        //Use structural exclusion of slices
        if ((ok == false) && (strcmp(argv[1], "-structural") == 0)) {
            argc--;
            argv++;
            structural=true;
            ok = true;
        }
        
        //Robust statistics for slices only
        if ((ok == false) && (strcmp(argv[1], "-exclude_slices_only") == 0)) {
            argc--;
            argv++;
            robust_slices_only = true;
            ok = true;
        }
        
        //Switch off registration
        if ((ok == false) && (strcmp(argv[1], "-no_registration") == 0)) {
            argc--;
            argv++;
            registration_flag=false;
            ok = true;
        }
        
        //Perform bias correction of the reconstructed image agains the GW image in the same motion correction iteration
        if ((ok == false) && (strcmp(argv[1], "-global_bias_correction") == 0)) {
            argc--;
            argv++;
            global_bias_correction=true;
            ok = true;
        }
        
        //Debug mode
        if ((ok == false) && (strcmp(argv[1], "-debug") == 0)) {
            argc--;
            argv++;
            debug=true;
            ok = true;
        }
        
        if (ok == false) {
            cerr << "Can not parse argument " << argv[1] << endl;
            usage();
        }
    }
    
    
    
    // -----------------------------------------------------------------------------
    
    string str_mirtk_path;
    string str_current_main_file_path;
    string str_current_exchange_file_path;
    
    string str_recon_path(current_mirtk_path);
    size_t pos = str_recon_path.find_last_of("\/");
    str_mirtk_path = str_recon_path.substr (0, pos);
    
    system("pwd > pwd.txt ");
    ifstream pwd_file("pwd.txt");
    
    if (pwd_file.is_open()) {
        getline(pwd_file, str_current_main_file_path);
        pwd_file.close();
    } else {
        cout << "System error: no rights to write in the current folder" << endl;
        exit(1);
    }
    
    str_current_exchange_file_path = str_current_main_file_path + "/tmp-file-exchange";
    
    if (str_current_exchange_file_path.length() > 0) {
        string remove_folder_cmd = "rm -r " + str_current_exchange_file_path + " > tmp-log.txt ";
        int tmp_log_rm = system(remove_folder_cmd.c_str());
        
        string create_folder_cmd = "mkdir " + str_current_exchange_file_path + " > tmp-log.txt ";
        int tmp_log_mk = system(create_folder_cmd.c_str());
        
    } else {
        cout << "System error: could not create a folder for file exchange" << endl;
        exit(1);
    }
    
    //---------------------------------------------------------------------------------------------
    
    
    
    if (rescale_stacks) {
        for (i=0;i<nStacks;i++)
            reconstruction->Rescale(stacks[i],1000);
    }
    
    
    //If transformations were not defined by user, set them to identity
    for (i=0;i<nStacks;i++) {
        RigidTransformation *rigidTransf = new RigidTransformation;
        stack_transformations.push_back(*rigidTransf);
        delete rigidTransf;
    }
    
    
    //Initialise 2*slice thickness if not given by user
    if (thickness.size()==0) {
        cout<< "Slice thickness : ";
        
        for (i=0;i<nStacks;i++) {
            double dx,dy,dz;
            stacks[i].GetPixelSize(&dx,&dy,&dz);
            thickness.push_back(dz*2);
            cout<<thickness[i]<<" ";
        }
        cout<<endl;
    }
    
    //Output volume
    RealImage reconstructed;
    
    
    //Set debug mode
    if (debug) reconstruction->DebugOn();
    else reconstruction->DebugOff();
    
    //Set force excluded slices
    reconstruction->SetForceExcludedSlices(force_excluded);
    
    //Set low intensity cutoff for bias estimation
    reconstruction->SetLowIntensityCutoff(low_intensity_cutoff)  ;
    
    
    // Check whether the template stack can be indentified
    if (templateNumber<0) {
        cerr<<"Please identify the template by assigning id transformation."<<endl;
        exit(1);
    }
    
    //If no mask was given - try to create mask from the template image in case it was padded
    if (mask == NULL) {
        mask = new RealImage(stacks[templateNumber]);
        *mask = reconstruction->CreateMask(*mask);
        cout << "Warning : no mask was provided " << endl;
    }
    
    
    //Before creating the template we will crop template stack according to the given mask
    if (mask != NULL)
    {
        //first resample the mask to the space of the stack
        //for template stact the transformation is identity
        RealImage m = *mask;
        reconstruction->TransformMask(stacks[templateNumber],m,stack_transformations[templateNumber]);
        
        //Crop template stack and prepare template for global volumetric registration
        
        maskedTemplate = stacks[templateNumber]*m;
        reconstruction->CropImage(stacks[templateNumber],m);
        reconstruction->CropImage(maskedTemplate,m);
        
        if (debug) {
            m.Write("maskforTemplate.nii.gz");
            stacks[templateNumber].Write("croppedTemplate.nii.gz");
        }
    }
    
    
    if (use_template) {
        
        RealImage m = *mask;
        RigidTransformation *rigidTransf = new RigidTransformation;
        reconstruction->TransformMask(template_stack,m,*rigidTransf);
        
        //Crop template stack and prepare template for global volumetric registration
        
        maskedTemplate = template_stack*m;
        reconstruction->CropImage(maskedTemplate,m);
        reconstruction->CropImage(template_stack,m);
        
    }
    
    
    
    //Create template volume with isotropic resolution
    //if resolution==0 it will be determined from in-plane resolution of the image
    resolution = reconstruction->CreateTemplate(maskedTemplate, resolution);
    
    //Set mask to reconstruction object.
    reconstruction->SetMask(mask,smooth_mask);
    
    //if remove_black_background flag is set, create mask from black background of the stacks
    if (remove_black_background) {
        reconstruction->CreateMaskFromBlackBackground(stacks, stack_transformations, smooth_mask);
        
    }
    
    //to redirect output from screen to text files
    
    //to remember cout and cerr buffer
    streambuf* strm_buffer = cout.rdbuf();
    streambuf* strm_buffer_e = cerr.rdbuf();
    //files for registration output
    string name;
    name = log_id+"log-registration.txt";
    ofstream file(name.c_str());
    name = log_id+"log-registration-error.txt";
    ofstream file_e(name.c_str());
    //files for reconstruction output
    name = log_id+"log-reconstruction->txt";
    ofstream file2(name.c_str());
    name = log_id+"log-evaluation.txt";
    ofstream fileEv(name.c_str());
    
    //set precision
    cout<<setprecision(3);
    cerr<<setprecision(3);
    
    //perform volumetric registration of the stacks
    //redirect output to files
    
    // if ( ! no_log ) {
    //     cerr.rdbuf(file_e.rdbuf());
    //     cout.rdbuf (file.rdbuf());
    // }
    
    cout << "------------------------------------------------------" << endl;
    
    
    if (debug)
        start = std::chrono::system_clock::now();
    //volumetric registration
    cout.rdbuf (file.rdbuf());
    reconstruction->StackRegistrations(stacks,stack_transformations,templateNumber);
    cout.rdbuf (strm_buffer);
    
    if (debug) {
        end = std::chrono::system_clock::now();
        elapsed_seconds = end-start;
        
        cout << "StackRegistrations ";
        cout << "- " << elapsed_seconds.count() << "s " << endl;
    }
    
    
    
    
    //redirect output back to screen
    
    // if ( ! no_log ) {
    //     cout.rdbuf (strm_buffer);
    //     cerr.rdbuf (strm_buffer_e);
    // }
    
    if (debug)
        start = std::chrono::system_clock::now();
    
    average = reconstruction->CreateAverage(stacks,stack_transformations);
    if (debug)
        average.Write("average1.nii.gz");
    
    if (debug) {
        end = std::chrono::system_clock::now();
        elapsed_seconds = end-start;
        
        cout << "CreateAverage ";
        cout << "- " << elapsed_seconds.count() << "s " << endl;
    }
    
    //Mask is transformed to the all other stacks and they are cropped
    for (i=0; i<nStacks; i++)
    {
        //template stack has been cropped already
        if ((i==templateNumber))
            continue;
        //transform the mask
        RealImage m=reconstruction->GetMask();
        reconstruction->TransformMask(stacks[i],m,stack_transformations[i]);
        //Crop template stack
        reconstruction->CropImage(stacks[i],m);
        
        if (debug) {
            sprintf(buffer,"mask%i.nii.gz",i);
            m.Write(buffer);
            sprintf(buffer,"cropped%i.nii.gz",i);
            stacks[i].Write(buffer);
        }
    }
    
    // we remove stacks of size 1 voxel (no intersection with ROI)
    Array<RealImage> selected_stacks;
    Array<RigidTransformation> selected_stack_transformations;
    int new_nStacks = 0;
    int new_templateNumber = 0;
    
    for (i=0; i<nStacks; i++) {
        if (stacks[i].GetX() == 1) {
            cerr << "stack " << i << " has no intersection with ROI" << endl;
            continue;
        }
        
        // we keep it
        selected_stacks.push_back(stacks[i]);
        selected_stack_transformations.push_back(stack_transformations[i]);
        
        if (i == templateNumber)
            new_templateNumber = templateNumber - (i-new_nStacks);
        
        new_nStacks++;
    }
    
    stacks.clear();
    stack_transformations.clear();
    nStacks = new_nStacks;
    templateNumber = new_templateNumber;
    
    for (i=0; i<nStacks; i++) {
        stacks.push_back(selected_stacks[i]);
        stack_transformations.push_back(selected_stack_transformations[i]);
    }
    
    // //Repeat volumetric registrations with cropped stacks
    // //redirect output to files
    // if ( ! no_log ) {
    //     cerr.rdbuf(file_e.rdbuf());
    //     cout.rdbuf (file.rdbuf());
    // }
    
    if (debug)
        start = std::chrono::system_clock::now();
    //volumetric registration
    
    cout.rdbuf (file.rdbuf());
    reconstruction->StackRegistrations(stacks,stack_transformations,templateNumber);
    cout.rdbuf (strm_buffer);
    
    if (debug) {
        end = std::chrono::system_clock::now();
        elapsed_seconds = end-start;
        cout << "StackRegistrations ";
        cout << "- " << elapsed_seconds.count() << "s " << endl;
    }
    
    
    
    // //redirect output back to screen
    // if ( ! no_log ) {
    //     cout.rdbuf (strm_buffer);
    //     cerr.rdbuf (strm_buffer_e);
    // }
    
    
    
    //Rescale intensities of the stacks to have the same average
    
    if (debug)
        start = std::chrono::system_clock::now();
    
    cout.rdbuf (file.rdbuf());
    if (intensity_matching)
        reconstruction->MatchStackIntensitiesWithMasking(stacks,stack_transformations,averageValue);
    else
        reconstruction->MatchStackIntensitiesWithMasking(stacks,stack_transformations,averageValue,true);
    cout.rdbuf (strm_buffer);
    
    if (debug) {
        end = std::chrono::system_clock::now();
        elapsed_seconds = end-start;
        cout << "MatchStackIntensitiesWithMasking ";
        cout << "- " << elapsed_seconds.count() << "s " << endl;
    }
    
    if (debug)
        start = std::chrono::system_clock::now();
    
    cout.rdbuf (file.rdbuf());
    average = reconstruction->CreateAverage(stacks,stack_transformations);
    cout.rdbuf (strm_buffer);
    if (debug)
        average.Write("average2.nii.gz");
    if (debug) {
        end = std::chrono::system_clock::now();
        elapsed_seconds = end-start;
        cout << "CreateAverage ";
        cout << "- " << elapsed_seconds.count() << "s " << endl;
    }
    
    //Create slices and slice-dependent transformations
    Array<RealImage> probability_maps;
    reconstruction->CreateSlicesAndTransformations(stacks, stack_transformations, thickness, probability_maps);
    

    //Mask all the slices
    cout.rdbuf (file.rdbuf());
    reconstruction->MaskSlices();
    cout.rdbuf (strm_buffer);
    
    //Set sigma for the bias field smoothing
    if (sigma>0)
        reconstruction->SetSigma(sigma);
    else
        reconstruction->SetSigma(20);
    
    //Set global bias correction flag
    if (global_bias_correction)
        reconstruction->GlobalBiasCorrectionOn();
    else
        reconstruction->GlobalBiasCorrectionOff();
    
    //if given read slice-to-volume registrations
    if (folder!=NULL)
        reconstruction->ReadTransformation(folder);
    
    //Initialise data structures for EM
    reconstruction->InitializeEM();
    
    //If registration was switched off - only 1 iteration is required
    cout.rdbuf (strm_buffer);
    if (!registration_flag) {
        iterations = 1;
    }
    
    //interleaved registration-reconstruction iterations
    for (int iter=0; iter<iterations; iter++) {
        //Print iteration number on the screen
        // if ( ! no_log ) {
        //     cout.rdbuf (strm_buffer);
        // }
//        cout << "------------------------------------------------------" << endl;
        cout << "------------------------------------------------------" << endl;
        cout<<"Iteration : " << iter << endl;
        
        reconstruction->MaskVolume();
        
        if (svr_only) {
            if (debug)
                start = std::chrono::system_clock::now();
            
            cout.rdbuf (file.rdbuf());
            if (remote_flag) {
                reconstruction->RemoteSliceToVolumeRegistration(iter, str_mirtk_path, str_current_main_file_path, str_current_exchange_file_path);
            } else {
                reconstruction->SliceToVolumeRegistration();
            }
            cout.rdbuf (strm_buffer);
            
            if (debug) {
                end = std::chrono::system_clock::now();
                elapsed_seconds = end-start;
                cout << "SliceToVolumeRegistration ";
                cout << "- " << elapsed_seconds.count() << "s " << endl;
            }
        }
        else {
            if (iter>0) {
                if((packages.size()>0)&&(iter<iterations-1)) {
                    
                    if (debug)
                        start = std::chrono::system_clock::now();
                    
//                    cout.rdbuf (file.rdbuf());
                    cout.rdbuf (strm_buffer);
                    reconstruction->PackageToVolume(stacks, packages, stack_transformations);
//                    cout.rdbuf (strm_buffer);
                    
                    if (debug) {
                        end = std::chrono::system_clock::now();
                        elapsed_seconds = end-start;
                        cout << "PackageToVolume ";
                        cout << "- " << elapsed_seconds.count() << "s " << endl;
                    }
                    
                    
                }
                else {
                    if (debug)
                        start = std::chrono::system_clock::now();

                    cout.rdbuf (file.rdbuf());
                    if (remote_flag) {
                        reconstruction->RemoteSliceToVolumeRegistration(iter, str_mirtk_path, str_current_main_file_path, str_current_exchange_file_path);
                    } else {
                        reconstruction->SliceToVolumeRegistration();
                    }
                    cout.rdbuf (strm_buffer);
                    
                    if (debug) {
                        end = std::chrono::system_clock::now();
                        elapsed_seconds = end-start;
                        cout << "SliceToVolumeRegistration ";
                        cout << "- " << elapsed_seconds.count() << "s " << endl;
                    }
                    
                }

            }
            
        }
        
        
        if (structural && iter>1) {
            
             cout.rdbuf (strm_buffer);
            
            if (debug)
                start = std::chrono::system_clock::now();
            
            reconstruction->StructuralExclusion();
            
            if (debug) {
                end = std::chrono::system_clock::now();
                elapsed_seconds = end-start;
                cout << "StructuralExclusion ";
                cout << "- " << elapsed_seconds.count() << "s " << endl;
            }
            
            cout.rdbuf (file.rdbuf());
            
        }
    
        
        //Set smoothing parameters
        //amount of smoothing (given by lambda) is decreased with improving alignment
        //delta (to determine edges) stays constant throughout
        cout.rdbuf (file.rdbuf());
        if(iter==(iterations-1))
            reconstruction->SetSmoothingParameters(delta,lastIterLambda);
        else
        {
            double l=lambda;
            for (i=0;i<levels;i++) {
                if (iter==iterations*(levels-i-1)/levels)
                    reconstruction->SetSmoothingParameters(delta, l);
                l*=2;
            }
        }
        cout.rdbuf (strm_buffer);
        
        //Use faster reconstruction during iterations and slower for final reconstruction
        if ( iter<(iterations-1) )
            reconstruction->SpeedupOn();
        else
            reconstruction->SpeedupOff();
        
        if(robust_slices_only)
            reconstruction->ExcludeWholeSlicesOnly();
        
        if (debug)
            start = std::chrono::system_clock::now();
        cout.rdbuf (file.rdbuf());
        reconstruction->InitializeEMValues();
        cout.rdbuf (strm_buffer);
        if (debug) {
            end = std::chrono::system_clock::now();
            elapsed_seconds = end-start;
            cout << "InitializeEMValues ";
            cout << "- " << elapsed_seconds.count() << "s " << endl;
        }
        
        
        if (debug)
            start = std::chrono::system_clock::now();
        //Calculate matrix of transformation between voxels of slices and volume
        
        // cout.rdbuf (file.rdbuf());
        reconstruction->CoeffInit();
        // cout.rdbuf (strm_buffer);
        if (debug) {
            end = std::chrono::system_clock::now();
            elapsed_seconds = end-start;
            cout << "CoeffInit ";
            cout << "- " << elapsed_seconds.count() << "s " << endl;
        }
        
        if (debug)
            start = std::chrono::system_clock::now();
        //Initialize reconstructed image with Gaussian weighted reconstruction
        
        cout.rdbuf (file.rdbuf());
        reconstruction->GaussianReconstruction();
        cout.rdbuf (strm_buffer);
        if (debug) {
            end = std::chrono::system_clock::now();
            elapsed_seconds = end-start;
            cout << "GaussianReconstruction ";
            cout << "- " << elapsed_seconds.count() << "s " << endl;
        }
        
        if (debug)
            start = std::chrono::system_clock::now();
        //Simulate slices (needs to be done after Gaussian reconstruction)
        
        cout.rdbuf (file.rdbuf());
        reconstruction->SimulateSlices();
        cout.rdbuf (strm_buffer);
        if (debug) {
            end = std::chrono::system_clock::now();
            elapsed_seconds = end-start;
            cout << "SimulateSlices ";
            cout << "- " << elapsed_seconds.count() << "s " << endl;
        }
        
        //Initialize robust statistics parameters
        cout.rdbuf (file.rdbuf());
        reconstruction->InitializeRobustStatistics();
        cout.rdbuf (strm_buffer);
        
        //EStep
        if(robust_statistics) {
            
            cout.rdbuf (file.rdbuf());
            reconstruction->EStep();
            cout.rdbuf (strm_buffer);
        }
        
        //number of reconstruction iterations
        if ( iter==(iterations-1) )
            rec_iterations = 20;
        else
            rec_iterations = 10;
        
        
        //reconstruction iterations
        i=0;
        for (i=0;i<rec_iterations;i++) {
            
            if (debug) {
                cout << "------------------------------------------------------" << endl;
                cout<<"Reconstruction iteration : "<<i<<endl;
            }
            
            if (intensity_matching) {
                
                //calculate bias fields
                if (debug)
                    start = std::chrono::system_clock::now();
                cout.rdbuf (file.rdbuf());
                if (sigma>0)
                    reconstruction->Bias();
                cout.rdbuf (strm_buffer);
                if (debug) {
                    end = std::chrono::system_clock::now();
                    elapsed_seconds = end-start;
                    cout << "Bias ";
                    cout << "- " << elapsed_seconds.count() << "s " << endl;
                }
                
                
                //calculate scales
                if (debug)
                    start = std::chrono::system_clock::now();
                cout.rdbuf (file.rdbuf());
                reconstruction->Scale();
                cout.rdbuf (strm_buffer);
                if (debug) {
                    end = std::chrono::system_clock::now();
                    elapsed_seconds = end-start;
                    cout << "Scale ";
                    cout << "- " << elapsed_seconds.count() << "s " << endl;
                }
                
            }
            
            //Update reconstructed volume
            if (debug)
                start = std::chrono::system_clock::now();
            
            cout.rdbuf (file.rdbuf());
            reconstruction->Superresolution(i+1);
            cout.rdbuf (strm_buffer);
            if (debug) {
                end = std::chrono::system_clock::now();
                elapsed_seconds = end-start;
                cout << "Superresolution ";
                cout << "- " << elapsed_seconds.count() << "s " << endl;
            }
            
            if (intensity_matching) {
                if (debug)
                    start = std::chrono::system_clock::now();
                cout.rdbuf (file.rdbuf());
                if((sigma>0)&&(!global_bias_correction))
                    reconstruction->NormaliseBias(i);
                cout.rdbuf (strm_buffer);
                if (debug) {
                    end = std::chrono::system_clock::now();
                    elapsed_seconds = end-start;
                    cout << "NormaliseBias ";
                    cout << "- " << elapsed_seconds.count() << "s " << endl;
                }
            }
            
            // Simulate slices (needs to be done
            // after the update of the reconstructed volume)
            if (debug)
                start = std::chrono::system_clock::now();
            
            cout.rdbuf (file.rdbuf());
            reconstruction->SimulateSlices();
            cout.rdbuf (strm_buffer);
            if (debug) {
                end = std::chrono::system_clock::now();
                elapsed_seconds = end-start;
                cout << "SimulateSlices ";
                cout << "- " << elapsed_seconds.count() << "s " << endl;
            }
            
            if(robust_statistics) {
                if (debug)
                    start = std::chrono::system_clock::now();
                
                cout.rdbuf (file.rdbuf());
                reconstruction->MStep(i+1);
                cout.rdbuf (strm_buffer);
                
                
                cout.rdbuf (file.rdbuf());
                reconstruction->EStep();
                cout.rdbuf (strm_buffer);
                
                if (debug) {
                    end = std::chrono::system_clock::now();
                    elapsed_seconds = end-start;
                    cout << "Robust statistics ";
                    cout << "- " << elapsed_seconds.count() << "s " << endl;
                }
            }
            
            //Save intermediate reconstructed image
            if (debug) {
                reconstructed=reconstruction->GetReconstructed();
                sprintf(buffer,"super%i.nii.gz",i);
                reconstructed.Write(buffer);
            }
            
            if (debug) {
                double error = reconstruction->EvaluateReconQuality(1);
                cout << "Total reconstruction error : " << error << endl; 
            }
            
            
        }//end of reconstruction iterations
        
        //Mask reconstructed image to ROI given by the mask
        reconstruction->MaskVolume();
        
        //Save reconstructed image
//        if (debug)
//        {
            reconstructed = reconstruction->GetReconstructed();
            sprintf(buffer, "image%i.nii.gz", iter);
            reconstructed.Write(buffer);
//        }
        
        
        
        //Evaluate - write number of included/excluded/outside/zero slices in each iteration in the file
        if ( ! no_log ) {
            cout.rdbuf (fileEv.rdbuf());
        }
        reconstruction->Evaluate(iter);
        // cout<<endl;
        if ( ! no_log ) {
            cout.rdbuf (strm_buffer);
        }
        
    } // end of interleaved registration-reconstruction iterations
    
    cout << "------------------------------------------------------" << endl;
    
    //save final result
    
    if (str_current_exchange_file_path.length() > 0) {
        string remove_folder_cmd = "rm -r " + str_current_exchange_file_path + " > tmp-log.txt ";
        int tmp_log_rm = system(remove_folder_cmd.c_str());
    }

    reconstruction->RestoreSliceIntensities();
    
    reconstruction->ScaleVolume();
    reconstructed=reconstruction->GetReconstructed();
    reconstructed.Write(output_name);
    
    cout << "Output volume : " << output_name << endl;
    
    end_total = std::chrono::system_clock::now();
    elapsed_seconds = end_total-start_total;
    cout << "Total time:" << elapsed_seconds.count() << "s " << endl;
    
    
    if (debug) {
        reconstruction->SaveTransformations();
        reconstruction->SaveSlices();
    }

    reconstruction->SaveSliceInfo();
    
    if ( info_filename.length() > 0 )
        reconstruction->SlicesInfo( info_filename.c_str(),
                                   stack_files );
    
    if(debug)
    {
        reconstruction->SaveWeights();
        reconstruction->SaveBiasFields();
        reconstruction->SimulateStacks(stacks);
        for (unsigned int i=0;i<stacks.size();i++)
        {
            sprintf(buffer,"simulated%i.nii.gz",i);
            stacks[i].Write(buffer);
        }
    }
    
    cout << "------------------------------------------------------" << endl;
    
    //The end of main()
    
    return 0;
}

