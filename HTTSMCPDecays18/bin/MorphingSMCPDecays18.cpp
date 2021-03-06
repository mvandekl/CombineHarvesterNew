#include <string>
#include <map>
#include <set>
#include <iostream>
#include <utility>
#include <vector>
#include <cstdlib>
#include "boost/algorithm/string/predicate.hpp"
#include "boost/program_options.hpp"
#include "boost/lexical_cast.hpp"
#include "boost/regex.hpp"
#include <boost/algorithm/string/replace.hpp>
#include "CombineHarvester/CombineTools/interface/CombineHarvester.h"
#include "CombineHarvester/CombineTools/interface/Observation.h"
#include "CombineHarvester/CombineTools/interface/Process.h"
#include "CombineHarvester/CombineTools/interface/Utilities.h"
#include "CombineHarvester/CombineTools/interface/CardWriter.h"
#include "CombineHarvester/CombineTools/interface/Systematics.h"
#include "CombineHarvester/CombineTools/interface/BinByBin.h"
#include "CombineHarvester/CombineTools/interface/Algorithm.h"
#include "CombineHarvester/CombineTools/interface/AutoRebin.h"
#include "CombineHarvester/CombineTools/interface/CopyTools.h"
#include "CombineHarvester/CombinePdfs/interface/MorphFunctions.h"
#include "CombineHarvester/HTTSMCPDecays18/interface/HttSystematics_SMRun2.h"
#include "CombineHarvester/CombineTools/interface/JsonTools.h"
#include "RooWorkspace.h"
#include "RooRealVar.h"
#include "TH2.h"
#include "TF1.h"
#include "TMatrix.h"
#include "TCanvas.h"

using namespace std;
using boost::starts_with;
namespace po = boost::program_options;

template <typename T>
void To1Bin(T* proc)
{
    std::unique_ptr<TH1> originalHist = proc->ClonedScaledShape();
    TH1F *hist = new TH1F("hist","hist",1,0,1);
    double err = 0;
    double rate =
    originalHist->IntegralAndError(0, originalHist->GetNbinsX() + 1, err);
    hist->SetDirectory(0);
    hist->SetBinContent(1, rate);
    hist->SetBinError(1, err);
    proc->set_shape(*hist, true);  // True means adjust the process rate to the
    // integral of the hist
}

bool CheckHistsMatch(TH1 *h1, TH1 *h2, double threshold){
  bool match = true;
  for (unsigned i=1; i<=(unsigned)h1->GetNbinsX(); ++i) {
    double c1 = h1->GetBinContent(i);
    double c2 = h2->GetBinContent(i);
    if(c1 != c2){
       match = false;
       break;
    }
  //  //if (c2 ==0 && c1 == 0) continue;
  //  //else if( c2== 0 ) {
  //  //  nomatch=false;
  //  //  break;  
  //  //}
  //  //if(fabs(c1/c2-1.)>threshold) {
  //  //  nomatch = false;
  //  //  break;
  //  //}
  }
  return match;
};

void ConvertShapesToLnN (ch::CombineHarvester& cb, string name, double threshold) {
  auto cb_syst = cb.cp().syst_name({name});
  cb_syst.ForEachSyst([&](ch::Systematic *syst) {
    if (syst->type().find("shape") != std::string::npos) {
      if(threshold<=0) {
        std::cout << "Converting systematic " << syst->name() << " for process " << syst->process() << " in bin " << syst->bin() << " to lnN." <<std::endl;
        syst->set_type("lnN");
        return;
      }
      //auto shape_u = syst->ClonedShapeU();
      //auto shape_d = syst->ClonedShapeD();

      //std::unique_ptr<TH1> nominal;

      //cb.cp().ForEachProc([&](ch::Process *proc){
      //  bool match_proc = (MatchingProcess(*proc,*syst));
      //  if(match_proc) nominal = proc->ClonedScaledShape(); 
      //});

      //bool match_u = false, match_d = false;

      //if(shape_u && nominal){
      //  match_u = CheckHistsMatch(nominal.get(),shape_u.get(), 1.-threshold); 
      //}
      //if(shape_d && nominal){
      //  match_d = CheckHistsMatch(nominal.get(),shape_d.get(),1.-threshold); 
      //} 
      //if(match_u && match_d){
      //  std::cout << "Converting systematic " << syst->name() << " for process " << syst->process() << " in bin " << syst->bin() << " to lnN as histograms match within specified threshold" <<std::endl;
      //  syst->set_type("lnN");
      //}
    }
  }); 

}

void DecorrelateMCAndEMB (ch::CombineHarvester& cb, string name, string embed_name, double scale) {
  auto cb_syst = cb.cp().process({"EmbedZTT"}).syst_name({name});
  double val = sqrt(1-scale*scale);
  ch::CloneSysts(cb_syst, cb, [&](ch::Systematic *s) {
      s->set_name(embed_name);
      if (s->type().find("shape") != std::string::npos) {
        s->set_scale(s->scale() * val);
      }
      if (s->type().find("lnN") != std::string::npos) {
        s->set_value_u((s->value_u() - 1.) * val + 1.);
        if (s->asymm()){
          s->set_value_d((s->value_d() - 1.) * val + 1.);
        }
      }
  });
  cb_syst.ForEachSyst([scale](ch::Systematic *syst) {
    if (syst->type().find("shape") != std::string::npos) {
      syst->set_scale(syst->scale() * scale);
    }
    if (syst->type().find("lnN") != std::string::npos) {
      syst->set_value_u((syst->value_u() - 1.) * scale + 1.);
      if (syst->asymm()){
        syst->set_value_d((syst->value_d() - 1.) * scale + 1.);
      }
    }
  });

}


void DecorrelateSyst (ch::CombineHarvester& cb, string name, double correlation, std::vector<string> chans_2016, std::vector<string> chans_2017, std::vector<string> chans_2018) {
  if (correlation >= 1.) return;
  auto cb_syst = cb.cp().syst_name({name});
  double val = sqrt(1. - correlation);
  // clone 2016 systs
  ch::CloneSysts(cb.cp().channel(chans_2016).syst_name({name}), cb, [&](ch::Systematic *s) {
      s->set_name(s->name()+"_2016");
      if (s->type().find("shape") != std::string::npos) {
        s->set_scale(s->scale() * val);
      }
      if (s->type().find("lnN") != std::string::npos) {
        s->set_value_u((s->value_u() - 1.) * val + 1.);
        if (s->asymm()){
          s->set_value_d((s->value_d() - 1.) * val + 1.);
        }
      }
  });
  // clone 2017 systs
  ch::CloneSysts(cb.cp().channel(chans_2017).syst_name({name}), cb, [&](ch::Systematic *s) {
      s->set_name(s->name()+"_2017");
      if (s->type().find("shape") != std::string::npos) {
        s->set_scale(s->scale() * val);
      }
      if (s->type().find("lnN") != std::string::npos) {
        s->set_value_u((s->value_u() - 1.) * val + 1.);
        if (s->asymm()){
          s->set_value_d((s->value_d() - 1.) * val + 1.);
        }
      }
  });
  // clone 2018 systs
  ch::CloneSysts(cb.cp().channel(chans_2018).syst_name({name}), cb, [&](ch::Systematic *s) {
      s->set_name(s->name()+"_2018");
      if (s->type().find("shape") != std::string::npos) {
        s->set_scale(s->scale() * val);
      }
      if (s->type().find("lnN") != std::string::npos) {
        s->set_value_u((s->value_u() - 1.) * val + 1.);
        if (s->asymm()){
          s->set_value_d((s->value_d() - 1.) * val + 1.);
        }
      }
  });

  if(correlation>0.) {
    // re-scale un-correlated part
    double val = sqrt(correlation);
    cb_syst.ForEachSyst([val](ch::Systematic *syst) {
      if (syst->type().find("shape") != std::string::npos) {
        syst->set_scale(syst->scale() * val);
      }
      if (syst->type().find("lnN") != std::string::npos) {
        syst->set_value_u((syst->value_u() - 1.) * val + 1.);
        if (syst->asymm()){
          syst->set_value_d((syst->value_d() - 1.) * val + 1.);
        }
      }
    });
  } else {
    // remove uncorrelated part if systs are 100% un-correlated
    cb.FilterSysts([&](ch::Systematic *s){
        return s->name().find(name) != std::string::npos && s->name().find("_2016") == std::string::npos && s->name().find("_2017") == std::string::npos && s->name().find("_2018") == std::string::npos;
    });

  }
}

void Remove13TeVFromNames (ch::CombineHarvester& cb) {
  auto cb_syst = cb.cp();
  cb.cp().ForEachSyst([&](ch::Systematic *syst) {
    std::string old_name = syst->name();
    if (old_name.find("lumi") == std::string::npos) {
      std::string new_name = old_name;
      boost::replace_all(new_name,"_13TeV","");
      syst->set_name(new_name);
    }  
  }
  );
}

void DecorrelateSystSeperateYears (ch::CombineHarvester& cb, string name, std::vector<double> correlations, std::vector<string> chans_2016, std::vector<string> chans_2017, std::vector<string> chans_2018) {
  auto cb_syst = cb.cp().syst_name({name});
  double val2016 = sqrt(1. - correlations[0]);
  double val2017 = sqrt(1. - correlations[1]);
  double val2018 = sqrt(1. - correlations[2]);
  // clone 2016 systs
  if(correlations[0] < 1.) {
    std::cout << "test1" << std::endl;
    ch::CloneSysts(cb.cp().channel(chans_2016).syst_name({name}), cb, [&](ch::Systematic *s) {
        s->set_name(s->name()+"_2016");
        if (s->type().find("shape") != std::string::npos) {
          s->set_scale(s->scale() * val2016);
        }
        if (s->type().find("lnN") != std::string::npos) {
          s->set_value_u((s->value_u() - 1.) * val2016 + 1.);
          if (s->asymm()){
            s->set_value_d((s->value_d() - 1.) * val2016 + 1.);
          }
        }
    });

    if(correlations[0]>0.) {
      // re-scale un-correlated part
      double val = sqrt(correlations[0]);
      cb_syst.channel(chans_2017).ForEachSyst([val](ch::Systematic *syst) {
        if (syst->type().find("shape") != std::string::npos) {
          syst->set_scale(syst->scale() * val);
        }
        if (syst->type().find("lnN") != std::string::npos) {
          syst->set_value_u((syst->value_u() - 1.) * val + 1.);
          if (syst->asymm()){
            syst->set_value_d((syst->value_d() - 1.) * val + 1.);
          }
        }
      });
    } else {
      // remove uncorrelated part if systs are 100% un-correlated
      cb_syst.channel(chans_2016).FilterSysts([&](ch::Systematic *s){
          return s->name().find(name) != std::string::npos && s->name().find("_2016") == std::string::npos && s->name().find("_2017") == std::string::npos && s->name().find("_2018") == std::string::npos;
      });
    }

  }
  // clone 2017 systs
  if(correlations[1] < 1.) {
    ch::CloneSysts(cb.cp().channel(chans_2017).syst_name({name}), cb, [&](ch::Systematic *s) {
        s->set_name(s->name()+"_2017");
        if (s->type().find("shape") != std::string::npos) {
          s->set_scale(s->scale() * val2017);
        }
        if (s->type().find("lnN") != std::string::npos) {
          s->set_value_u((s->value_u() - 1.) * val2017 + 1.);
          if (s->asymm()){
            s->set_value_d((s->value_d() - 1.) * val2017 + 1.);
          }
        }
    });

    if(correlations[1]>0.) {
      // re-scale un-correlated part
      double val = sqrt(correlations[1]);
      cb_syst.channel(chans_2017).ForEachSyst([val](ch::Systematic *syst) {
        if (syst->type().find("shape") != std::string::npos) {
          syst->set_scale(syst->scale() * val);
        }
        if (syst->type().find("lnN") != std::string::npos) {
          syst->set_value_u((syst->value_u() - 1.) * val + 1.);
          if (syst->asymm()){
            syst->set_value_d((syst->value_d() - 1.) * val + 1.);
          }
        }
      });
    } else {
      // remove uncorrelated part if systs are 100% un-correlated
      cb_syst.channel(chans_2017).FilterSysts([&](ch::Systematic *s){
          return s->name().find(name) != std::string::npos && s->name().find("_2016") == std::string::npos && s->name().find("_2017") == std::string::npos && s->name().find("_2018") == std::string::npos;
      });
    }
  }
  // clone 2018 systs
  if(correlations[2] < 1.) {
    ch::CloneSysts(cb.cp().channel(chans_2018).syst_name({name}), cb, [&](ch::Systematic *s) {
        s->set_name(s->name()+"_2018");
        if (s->type().find("shape") != std::string::npos) {
          s->set_scale(s->scale() * val2018);
        }
        if (s->type().find("lnN") != std::string::npos) {
          s->set_value_u((s->value_u() - 1.) * val2018 + 1.);
          if (s->asymm()){
            s->set_value_d((s->value_d() - 1.) * val2018 + 1.);
          }
        }
    });

    if(correlations[2]>0.) {
      // re-scale un-correlated part
      double val = sqrt(correlations[2]);
      cb_syst.channel(chans_2018).ForEachSyst([val](ch::Systematic *syst) {
        if (syst->type().find("shape") != std::string::npos) {
          syst->set_scale(syst->scale() * val);
        }
        if (syst->type().find("lnN") != std::string::npos) {
          syst->set_value_u((syst->value_u() - 1.) * val + 1.);
          if (syst->asymm()){
            syst->set_value_d((syst->value_d() - 1.) * val + 1.);
          }
        }
      });
    } else {
      // remove uncorrelated part if systs are 100% un-correlated
      cb_syst.channel(chans_2018).FilterSysts([&](ch::Systematic *s){
          return s->name().find(name) != std::string::npos && s->name().find("_2016") == std::string::npos && s->name().find("_2017") == std::string::npos && s->name().find("_2018") == std::string::npos;
      });
    }
  }

}

int main(int argc, char** argv) {

    string output_folder = "sm_run2";
    string input_folder_em="IC/";
    string input_folder_et="IC/";
    string input_folder_mt="DESY/";
    string input_folder_tt="IC/";
    string input_folder_mm="USCMS/";
    string scale_sig_procs="";
    string postfix="";
    bool ttbar_fit = false;
    unsigned no_shape_systs = 0;
    bool do_embedding = true;
    bool auto_rebin = false;
    bool do_jetfakes = true;
    bool mergeXbbb = false; 
    bool mergeSymm = false; 
    bool control = false; 
    unsigned backgroundOnly = 0; 

    string era;
    po::variables_map vm;
    po::options_description config("configuration");
    config.add_options()
    ("input_folder_em", po::value<string>(&input_folder_em)->default_value("IC/"))
    ("input_folder_et", po::value<string>(&input_folder_et)->default_value("IC/"))
    ("input_folder_mt", po::value<string>(&input_folder_mt)->default_value("DESY/"))
    ("input_folder_tt", po::value<string>(&input_folder_tt)->default_value("IC/"))
    ("input_folder_mm", po::value<string>(&input_folder_mm)->default_value("USCMS"))
    ("postfix", po::value<string>(&postfix)->default_value(postfix))
    ("output_folder", po::value<string>(&output_folder)->default_value("sm_run2"))
    ("no_shape_systs", po::value<unsigned>(&no_shape_systs)->default_value(no_shape_systs))
    ("do_embedding", po::value<bool>(&do_embedding)->default_value(true))
    ("do_jetfakes", po::value<bool>(&do_jetfakes)->default_value(true))
    ("auto_rebin", po::value<bool>(&auto_rebin)->default_value(false))
    ("era", po::value<string>(&era)->default_value("all"))
    ("ttbar_fit", po::value<bool>(&ttbar_fit)->default_value(false))
    ("mergeXbbb", po::value<bool>(&mergeXbbb)->default_value(false))
    ("mergeSymm", po::value<bool>(&mergeSymm)->default_value(false))
    ("control", po::value<bool>(&control)->default_value(false))
    ("backgroundOnly", po::value<unsigned>(&backgroundOnly)->default_value(0));


    po::store(po::command_line_parser(argc, argv).options(config).run(), vm);
    po::notify(vm);
    typedef vector<string> VString;
 
    if(mergeXbbb) postfix+="-mergeXbins";
    if(mergeSymm) postfix+="-symm";

    VString years;
    if ( era.find("2016") != std::string::npos ) years.push_back("2016");
    if ( era.find("2017") != std::string::npos ) years.push_back("2017");
    if ( era.find("2018") != std::string::npos ) years.push_back("2018");
    if ( era=="all" ) years = {"2016","2017","2018"};
 
    typedef vector<pair<int, string>> Categories;
    //! [part1]
    // First define the location of the "auxiliaries" directory where we can
    // source the input files containing the datacard shapes
    //    string aux_shapes = string(getenv("CMSSW_BASE")) + "/src/CombineHarvester/CombineTools/bin/AllROOT_20fb/";
    std::map<string, string> input_dir;
    input_dir["em"]  = string(getenv("CMSSW_BASE")) + "/src/CombineHarvester/HTTSMCPDecays18/shapes/"+input_folder_em+"/";
    input_dir["mt"]  = string(getenv("CMSSW_BASE")) + "/src/CombineHarvester/HTTSMCPDecays18/shapes/"+input_folder_mt+"/";
    input_dir["et"]  = string(getenv("CMSSW_BASE")) + "/src/CombineHarvester/HTTSMCPDecays18/shapes/"+input_folder_et+"/";
    input_dir["tt"]  = string(getenv("CMSSW_BASE")) + "/src/CombineHarvester/HTTSMCPDecays18/shapes/"+input_folder_tt+"/";
    input_dir["ttbar"]  = string(getenv("CMSSW_BASE")) + "/src/CombineHarvester/HTTSMCPDecays18/shapes/"+input_folder_em+"/";    
    
    
    VString chns = {"tt","mt"};
    if (ttbar_fit) chns.push_back("ttbar");
    
    map<string, VString> bkg_procs;
    bkg_procs["et"] = {"ZTT", "QCD", "ZL", "ZJ","TTT","TTJ", "VVT", "VVJ"/*, "EWKZ"*/, "W"};
    bkg_procs["mt"] = {"ZTT", "QCD", "ZL", "ZJ","TTT","TTJ", "VVT", "VVJ"/*, "EWKZ"*/, "W"};
    bkg_procs["tt"] = {"ZTT", "W", "QCD", "ZL", "ZJ","TTT","TTJ",  "VVT","VVJ"/*, "EWKZ"*/};
    bkg_procs["em"] = {"ZTT","W", "QCD", "ZLL", "TT", "VV"/*, "EWKZ"*/};
    bkg_procs["ttbar"] = {"ZTT", "W", "QCD", "ZLL", "TT", "VV"/*, "EWKZ"*/};
    
    if(do_embedding){
      bkg_procs["et"] = {"EmbedZTT", "QCD", "ZL", "ZJ","TTT","TTJ", "VVT", "VVJ", "W"/*, "EWKZ"*/};
      bkg_procs["mt"] = {"EmbedZTT", "QCD", "ZL", "ZJ","TTT","TTJ",  "VVT", "VVJ", "W"/*, "EWKZ"*/};
      bkg_procs["tt"] = {"EmbedZTT", "W", "QCD", "ZL", "ZJ","TTT","TTJ",  "VVT","VVJ"/*, "EWKZ"*/};
      bkg_procs["em"] = {"EmbedZTT","W", "QCD", "ZLL", "TT", "VV"/*, "EWKZ"*/};
      bkg_procs["ttbar"] = {"EmbedZTT", "W", "QCD", "ZLL", "TT", "VV"/*, "EWKZ"*/};
    }

    if(do_jetfakes){
      bkg_procs["et"] = {"ZTT", "ZL", "TTT", "VVT"/*, "EWKZ"*/, "jetFakes"};
      bkg_procs["mt"] = {"ZTT", "ZL", "TTT", "VVT"/*, "EWKZ"*/, "jetFakes"};
      bkg_procs["tt"] = {"ZTT", "ZL", "TTT", "VVT"/*, "EWKZ"*/, "jetFakes"};

      if(do_embedding){
        bkg_procs["et"] = {"EmbedZTT", "ZL", "TTT", "VVT", "jetFakes"/*, "EWKZ"*/};
        bkg_procs["mt"] = {"EmbedZTT", "ZL", "TTT", "VVT", "jetFakes"/*, "EWKZ"*/};
        bkg_procs["tt"] = {"EmbedZTT", "ZL", "TTT", "VVT", "jetFakes", "Wfakes"/*"EWKZ"*/};
      }

    }


    ch::CombineHarvester cb;
    
    map<string,Categories> cats;
    
    if( era.find("2016") != std::string::npos ||  era.find("all") != std::string::npos) {
      cats["tt_2016"] = {
        {1, "tt_2016_zttEmbed"},
        {2, "tt_2016_jetFakes"},
        {3, "tt_2016_higgs_Rho_Rho"},
        {4, "tt_2016_higgs_0A1_Rho_and_0A1_0A1"},
        {5, "tt_2016_higgs_A1_Rho"},
        {6, "tt_2016_higgs_A1_A1"},
        {7, "tt_2016_higgs_Pi_Rho_Mixed"},
        {8, "tt_2016_higgs_Pi_Pi"},
        {9, "tt_2016_higgs_Pi_A1_Mixed"},
        {10, "tt_2016_higgs_Pi_0A1_Mixed"},
        {11, "tt_2016_higgs_A1_0A1"},
        //{11, "tt_2016_higgs_other"},
      };
      cats["mt_2016"] = {
        {1, "mt_ztt_2016"},
        {2, "mt_fakes_2016"},
        {3, "mt_murho_sig_2016"},
	{4, "mt_mupi_sig_2016"},
	{5, "mt_mua1_sig_2016"},
	{6, "mt_mu0a1_sig_2016"},
      };
    }  
    if( era.find("2017") != std::string::npos ||  era.find("all") != std::string::npos) {
      cats["tt_2017"] = {
        {1, "tt_2017_zttEmbed"},
        {2, "tt_2017_jetFakes"},
        {3, "tt_2017_higgs_Rho_Rho"},
        {4, "tt_2017_higgs_0A1_Rho_and_0A1_0A1"},
        {5, "tt_2017_higgs_A1_Rho"},
        {6, "tt_2017_higgs_A1_A1"},
        {7, "tt_2017_higgs_Pi_Rho_Mixed"},
        {8, "tt_2017_higgs_Pi_Pi"},
        {9, "tt_2017_higgs_Pi_A1_Mixed"},
        {10, "tt_2017_higgs_Pi_0A1_Mixed"},
        {11, "tt_2017_higgs_A1_0A1"},
        //{11, "tt_2017_higgs_other"},
      };
      cats["mt_2017"] = {
        {1, "mt_ztt_2017"},
        {2, "mt_fakes_2017"},
        {3, "mt_murho_sig_2017"},
        {4, "mt_mupi_sig_2017"},
        {5, "mt_mua1_sig_2017"},
        {6, "mt_mu0a1_sig_2017"},
      };
    }
    if( era.find("2018") != std::string::npos ||  era.find("all") != std::string::npos) {
      cats["tt_2018"] = {
        {1, "tt_2018_zttEmbed"},
        {2, "tt_2018_jetFakes"},
        {3, "tt_2018_higgs_Rho_Rho"},
        {4, "tt_2018_higgs_0A1_Rho_and_0A1_0A1"},
        {5, "tt_2018_higgs_A1_Rho"},
        {6, "tt_2018_higgs_A1_A1"},
        {7, "tt_2018_higgs_Pi_Rho_Mixed"},
        {8, "tt_2018_higgs_Pi_Pi"},
        {9, "tt_2018_higgs_Pi_A1_Mixed"}, 
        {10, "tt_2018_higgs_Pi_0A1_Mixed"}, 
        {11, "tt_2018_higgs_A1_0A1"},
        //{11, "tt_2018_higgs_other"},
      };
      cats["mt_2018"] = {
        {1, "mt_ztt_2018"},
        {2, "mt_fakes_2018"},
        {3, "mt_murho_sig_2018"},
        {4, "mt_mupi_sig_2018"},
        {5, "mt_mua1_sig_2018"},
        {6, "mt_mu0a1_sig_2018"},
      };
    }
    
    if(backgroundOnly==1) {
      for (string y : {"2016","2017","2018"}) {
        cats["mt_"+y] = {
          {3, "mt_murho_ztt_"+y},
          {4, "mt_mupi_ztt_"+y},
          {5, "mt_mua1_ztt_"+y},
          {6, "mt_mu0a1_ztt_"+y},
        };
        cats["tt_"+y] = {
          {3, "tt_"+y+"_zttEmbed_Rho_Rho"},
          {4, "tt_"+y+"_zttEmbed_0A1_Rho_and_0A1_0A1"},
          {5, "tt_"+y+"_zttEmbed_A1_Rho"},
          {6, "tt_"+y+"_zttEmbed_A1_A1"},
          {7, "tt_"+y+"_zttEmbed_Pi_Rho_Mixed"},
          {8, "tt_"+y+"_zttEmbed_Pi_Pi"},
          {9, "tt_"+y+"_zttEmbed_Pi_A1_Mixed"},
          {10, "tt_"+y+"_zttEmbed_Pi_0A1_Mixed"},
          {11, "tt_"+y+"_zttEmbed_A1_0A1"},
                };
      }
    }

    if(backgroundOnly==2) {
      for (string y : {"2016","2017","2018"}) {
        cats["mt_"+y] = {
          {3, "mt_murho_fakes_"+y},
          {4, "mt_mupi_fakes_"+y},
          {5, "mt_mua1_fakes_"+y},
          {6, "mt_mu0a1_fakes_"+y},
        };
        cats["tt_"+y] = {
          {3, "tt_"+y+"_jetFakes_Rho_Rho"},
          {4, "tt_"+y+"_jetFakes_0A1_Rho_and_0A1_0A1"},
          {5, "tt_"+y+"_jetFakes_A1_Rho"},
          {6, "tt_"+y+"_jetFakes_A1_A1"},
          {7, "tt_"+y+"_jetFakes_Pi_Rho_Mixed"},
          {8, "tt_"+y+"_jetFakes_Pi_Pi"},
          {9, "tt_"+y+"_jetFakes_Pi_A1_Mixed"},
          {10, "tt_"+y+"_jetFakes_Pi_0A1_Mixed"},
          {11, "tt_"+y+"_jetFakes_A1_0A1"},
                };
      }
    }

    if(control) {
      for (string y : years) {
        cats["tt_"+y] = {
          {1, "tt_"+y+"_m_vis"},
          {2, "tt_"+y+"_svfit_mass"},
          {3, "tt_"+y+"_pt_1"},
          {4, "tt_"+y+"_pt_2"},
          {5, "tt_"+y+"_n_jets"},
          {6, "tt_"+y+"_met"},
          {7, "tt_"+y+"_mjj"},
        };
      }
    }


    map<string, VString> sig_procs;
    sig_procs["ggH"] = {"ggH_sm_htt", "ggH_ps_htt", "ggH_mm_htt"};
    sig_procs["qqH"] = {"qqH_sm_htt", "qqH_ps_htt", "qqH_mm_htt", "WH_sm_htt", "WH_ps_htt", "WH_mm_htt", "ZH_sm_htt", "ZH_ps_htt", "ZH_mm_htt"};   

    vector<string> masses = {"125"};    
    
    using ch::syst::bin_id;
    using ch::JoinStr;
 
    //! [part2]
    for(auto year: years) {
      for (auto chn : chns) {
          cb.AddObservations({"*"}, {"htt"}, {"13TeV"}, {chn+"_"+year}, cats[chn+"_"+year]);

          cb.AddProcesses(   {"*"}, {"htt"}, {"13TeV"}, {chn+"_"+year}, bkg_procs[chn], cats[chn+"_"+year], false);

          if(chn == "em" || chn == "et" || chn == "mt" || chn == "tt"){
            cb.AddProcesses(masses,   {"htt"}, {"13TeV"}, {chn+"_"+year}, sig_procs["qqH"], cats[chn+"_"+year], true); // SM VBF/VH are added as signal

            cb.AddProcesses(masses,   {"htt"}, {"13TeV"}, {chn+"_"+year}, sig_procs["ggH"], cats[chn+"_"+year], true);

          }
      }
    } 
    //! [part4]
    
    
    ch::AddSMRun2Systematics(cb, 0, ttbar_fit, false);
    
    if(no_shape_systs==1){
      cb.FilterSysts([&](ch::Systematic *s){
        return s->type().find("shape") != std::string::npos;
      });
    } else if (no_shape_systs==2){
      // this option will only filter systamtics that required seperate trees to produce. shape systematics made from weights will not be removed
      cb.FilterSysts([&](ch::Systematic *s){
        return s->name().find("scale_t") != std::string::npos || s->name().find("scale_e") != std::string::npos || s->name().find("scale_j") != std::string::npos || s->name().find("_met_") != std::string::npos || s->name().find("ZLShape") != std::string::npos;
      });
    }

    //! [part7]
    for(auto year: years) {
      for (string chn : chns){
          string channel = chn;
          string extra = "";
          extra = "/"+year+"/";
          if(chn == "ttbar") channel = "em"; 
          cb.cp().channel({chn+"_"+year}).backgrounds().ExtractShapes(
            input_dir[chn] + extra + "htt_"+channel+".inputs-sm-13TeV"+postfix+".root",
            "$BIN/$PROCESS",
            "$BIN/$PROCESS_$SYSTEMATIC"
          );
          if(chn == "em" || chn == "et" || chn == "mt" || chn == "tt"){
            cb.cp().channel({chn+"_"+year}).process(sig_procs["ggH"]).ExtractShapes(
              input_dir[chn] + extra + "htt_"+chn+".inputs-sm-13TeV"+postfix+".root",
              "$BIN/$PROCESS$MASS",
              "$BIN/$PROCESS$MASS_$SYSTEMATIC"
            );
            cb.cp().channel({chn+"_"+year}).process(sig_procs["qqH"]).ExtractShapes(
              input_dir[chn] + extra +  "htt_"+chn+".inputs-sm-13TeV"+postfix+".root",
              "$BIN/$PROCESS$MASS",
              "$BIN/$PROCESS$MASS_$SYSTEMATIC"
            );
          }
      }
    }    

    
    //Now delete processes with 0 yield
    cb.FilterProcs([&](ch::Process *p) {
        bool null_yield = !(p->rate() > 0.);
        if (null_yield){
            std::cout << "[Null yield] Removing process with null yield: \n ";
            std::cout << ch::Process::PrintHeader << *p << "\n";
            cb.FilterSysts([&](ch::Systematic *s){
                bool remove_syst = (MatchingProcess(*p,*s));
                return remove_syst;
            });
        }
        return null_yield;
    });   

    // effectivly remove problematic systs here
    cb.cp().syst_name({"CMS_res_j_13TeV","CMS_scale_j_Absolute_13TeV","CMS_scale_j_BBEC1_13TeV","CMS_scale_j_FlavorQCD_13TeV"}).channel({"tt_2017"}).process({"ZL"}).bin_id({9}).ForEachSyst([](ch::Systematic *sys) {
        sys->set_type("lnN");
        sys->set_value_d(1.);
        sys->set_value_u(1.);
    });

    
    // And convert any shapes in the ttbar CRs to lnN:
    // Convert all shapes to lnN at this stage

    cb.cp().channel({"ttbar_2016","ttbar_2017"}).syst_type({"shape"}).ForEachSyst([](ch::Systematic *sys) {
        sys->set_type("lnN");
        if(sys->value_d() <0.001) {sys->set_value_d(0.001);};
        if(sys->value_u() <0.001) {sys->set_value_u(0.001);};
    });

    
    
    // can auto-merge the bins with bbb uncertainty > 90% - may be better to merge these bins by hand though!
    auto rebin = ch::AutoRebin()
    .SetBinThreshold(0.)
    .SetBinUncertFraction(0.9)
    .SetRebinMode(1)
    .SetPerformRebin(true)
    .SetVerbosity(1);
    if(auto_rebin) rebin.Rebin(cb, cb);
  
  
    // At this point we can fix the negative bins
    std::cout << "Fixing negative bins\n";
    cb.ForEachProc([](ch::Process *p) {
      if (ch::HasNegativeBins(p->shape())) {
         std::cout << "[Negative bins] Fixing negative bins for " << p->bin()
                   << "," << p->process() << "\n";
        auto newhist = p->ClonedShape();
        ch::ZeroNegativeBins(newhist.get());
        // Set the new shape but do not change the rate, we want the rate to still
        // reflect the total integral of the events
        p->set_shape(std::move(newhist), false);
      }
    });
  
    cb.ForEachSyst([](ch::Systematic *s) {
      if (s->type().find("shape") == std::string::npos) return;            
      if (ch::HasNegativeBins(s->shape_u()) || ch::HasNegativeBins(s->shape_d())) {
         std::cout << "[Negative bins] Fixing negative bins for syst" << s->bin()
               << "," << s->process() << "," << s->name() << "\n";
        auto newhist_u = s->ClonedShapeU();
        auto newhist_d = s->ClonedShapeD();
        ch::ZeroNegativeBins(newhist_u.get());
        ch::ZeroNegativeBins(newhist_d.get());
        // Set the new shape but do not change the rate, we want the rate to still
        // reflect the total integral of the events
        s->set_shapes(std::move(newhist_u), std::move(newhist_d), nullptr);
      }
  });

  cb.ForEachSyst([](ch::Systematic *s) {
      if (s->type().find("shape") == std::string::npos) return;
      if(!(s->value_d()<0.001 || s->value_u()<0.001)) return;
      std::cout << "[Negative yield] Fixing negative yield for syst" << s->bin()
               << "," << s->process() << "," << s->name() << "\n";
      if(s->value_u()<0.001){
         s->set_value_u(0.001);
         s->set_type("lnN");
      }
      if(s->value_d()<0.001){
         s->set_value_d(0.001);
         s->set_type("lnN");
      }
  });

  // convert systematics to lnN here
  ConvertShapesToLnN(cb.cp().backgrounds(), "CMS_eff_b_13TeV", 0.);
  cb.cp().RenameSystematic(cb,"CMS_eff_b_13TeV","CMS_btag_comb");

  // for high pT tau ID uncertainties for tt channel, these can only affect normalizations in the MVA-DM exclusive categories
  ConvertShapesToLnN(cb.cp().channel({"tt_2016","tt_2017","tt_2018"}).bin_id({1,2},false), "CMS_eff_t_pThigh_MVADM0_13TeV", 0.);
  ConvertShapesToLnN(cb.cp().channel({"tt_2016","tt_2017","tt_2018"}).bin_id({1,2},false), "CMS_eff_t_pThigh_MVADM1_13TeV", 0.);
  ConvertShapesToLnN(cb.cp().channel({"tt_2016","tt_2017","tt_2018"}).bin_id({1,2},false), "CMS_eff_t_pThigh_MVADM2_13TeV", 0.);
  ConvertShapesToLnN(cb.cp().channel({"tt_2016","tt_2017","tt_2018"}).bin_id({1,2},false), "CMS_eff_t_pThigh_MVADM10_13TeV", 0.);

  // For mu->tau fake energy scale templates there is no clear shape effects for 1prong1pi temnplates so convert to lnN

  std::vector<std::string> jes_systs = {
    "CMS_scale_j_Absolute_13TeV",
    "CMS_scale_j_BBEC1_13TeV",
    "CMS_scale_j_EC2_13TeV",
    "CMS_scale_j_FlavorQCD_13TeV",          
    "CMS_scale_j_HF_13TeV",
    "CMS_scale_j_RelativeBal_13TeV",
    "CMS_scale_j_Absolute_2016_13TeV",
    "CMS_scale_j_Absolute_2017_13TeV",
    "CMS_scale_j_Absolute_2018_13TeV",
    "CMS_scale_j_BBEC1_2016_13TeV",
    "CMS_scale_j_BBEC1_2017_13TeV",
    "CMS_scale_j_BBEC1_2018_13TeV",
    "CMS_scale_j_EC2_2016_13TeV",
    "CMS_scale_j_EC2_2017_13TeV",
    "CMS_scale_j_EC2_2018_13TeV",
    "CMS_scale_j_HF_2016_13TeV", 
    "CMS_scale_j_HF_2017_13TeV", 
    "CMS_scale_j_HF_2018_13TeV", 
    "CMS_scale_j_RelativeSample_2016_13TeV",
    "CMS_scale_j_RelativeSample_2017_13TeV",
    "CMS_scale_j_RelativeSample_2018_13TeV",
    "CMS_res_j_13TeV",
    "CMS_scale_met_unclustered_13TeV",
    "CMS_htt_boson_reso_met_13TeV",
    "CMS_htt_boson_scale_met_13TeV"
  };

  std::vector<std::string> tes_systs = {
    "CMS_scale_t_1prong_13TeV",
    "CMS_scale_t_1prong1pizero_13TeV",
    "CMS_scale_t_3prong_13TeV",
    "CMS_scale_t_3prong1pizero_13TeV"
  };

  for (auto jes : jes_systs) {
    ConvertShapesToLnN(cb.cp().backgrounds().channel({"tt_2016","tt_2017","tt_2018"}).process({"ZL","Wfakes","VVT"}),jes,0.);
    ConvertShapesToLnN(cb.cp().backgrounds().channel({"tt_2016","tt_2018"}).process({"TTT"}).bin_id({1,2,3,7},false),jes,0.);
    ConvertShapesToLnN(cb.cp().backgrounds().channel({"tt_2017"}).process({"TTT"}),jes,0.); // worse stats in 2017 for some reason so we have to conver to lnN for other categories - check
    ConvertShapesToLnN(cb.cp().backgrounds().channel({"mt_2016","mt_2017","mt_2018"}).process({"ZL","VVT"}).bin_id({4,5,6}),jes,0.);
  }
  ConvertShapesToLnN(cb.cp().backgrounds().bin_id({3},false),"CMS_htt_ZLShape_mt_1prong1pi_13TeV",0.);
  for (auto tes : tes_systs) {
    ConvertShapesToLnN(cb.cp().backgrounds().channel({"tt_2016","tt_2017","tt_2018"}).process({"Wfakes","VVT"}),tes,0.);
    ConvertShapesToLnN(cb.cp().backgrounds().channel({"tt_2016","tt_2018"}).process({"TTT"}).bin_id({1,2,3,7},false),tes,0.);
    ConvertShapesToLnN(cb.cp().backgrounds().channel({"tt_2017"}).process({"TTT"}),tes,0.);
    ConvertShapesToLnN(cb.cp().backgrounds().channel({"mt_2016","mt_2017","mt_2018"}).process({"VVT"}).bin_id({4,5,6}),tes,0.);
  }

  // remove uncertainties which are dominated by statistical fluctuations so are unphysical
  cb.cp().channel({"mt_2016","mt_2017","mt_2018"}).syst_name(JoinStr({{"CMS_scale_mu_13TeV","CMS_htt_ZLShape_mt_1prong1pi_13TeV"},jes_systs})).process({"ZL"}).bin_id({5,6}).ForEachSyst([](ch::Systematic *sys) {
        sys->set_type("lnN");
        sys->set_value_d(1.);
        sys->set_value_u(1.);
  }); 
  cb.cp().channel({"tt_2016","tt_2017","tt_2018"}).syst_name(JoinStr({tes_systs,jes_systs})).process({"ZL","VVT","Wfakes"}).bin_id({1,2},false).ForEachSyst([](ch::Systematic *sys) {
        sys->set_type("lnN");
        sys->set_value_d(1.);
        sys->set_value_u(1.);
  });
  cb.cp().channel({"tt_2017"}).syst_name(JoinStr({tes_systs,jes_systs})).process({"TTT"}).bin_id({1,2},false).ForEachSyst([](ch::Systematic *sys) {
        sys->set_type("lnN");
        sys->set_value_d(1.);
        sys->set_value_u(1.);
  }); 
  cb.cp().channel({"tt_2016","tt_2018"}).syst_name(JoinStr({tes_systs,jes_systs})).process({"TTT"}).bin_id({1,2,3},false).ForEachSyst([](ch::Systematic *sys) {
        sys->set_type("lnN");
        sys->set_value_d(1.);
        sys->set_value_u(1.);
  });
  // these uncertainty that effectivly don't do anything will be removed at a later stage

  // If any shapes are identical then change these uncertainties to lnN - they will then be removed altogether in a latter step if the yields also match
  // Be careful with this part as you could miss bug in the code which might mean the systematic is not implemented properly - suggest this is kept commented until the very end
  cb.ForEachSyst([&](ch::Systematic *s) {
    if (s->type().find("shape") == std::string::npos) return;
      std::unique_ptr<TH1> nominal;

      cb.cp().ForEachProc([&](ch::Process *proc){
         bool match_proc = (MatchingProcess(*proc,*s));
         if(match_proc) nominal = proc->ClonedShape(); 
       });
    auto up = s->ClonedShapeU();
    auto down = s->ClonedShapeD();
    bool match_up = false, match_down = false;
    match_up = CheckHistsMatch(up.get(), nominal.get(), 0.) ;
    match_down = CheckHistsMatch(down.get(), nominal.get(), 0.) ;

    if(match_up && match_down) {
      std::cout << "Systematic teamplates match exactly for: \n" << *s << "\n" << "changing it to lnN!" << std::endl;
      s->set_type("lnN"); 
    }
   });

    if(mergeXbbb) {
      // if we are mergin bbb's we can't use autoMC stats
      auto bbb_fakes = ch::BinByBinFactory()
      .SetPattern("CMS_$ANALYSIS_$CHANNEL_$BIN_$ERA_$PROCESS_bbb_bin_$#") // this needs to have "_bbb_bin_" in the pattern for the mergeXbbb option to work
      .SetAddThreshold(0.)
      .SetMergeThreshold(0.5)
      .SetFixNorm(false);
      bbb_fakes.MergeBinErrors(cb.cp().backgrounds().process({"jetFakes"}));
      bbb_fakes.AddBinByBin(cb.cp().backgrounds().process({"jetFakes"}), cb);

      auto bbb_real = ch::BinByBinFactory()
      .SetPattern("CMS_$ANALYSIS_$CHANNEL_$BIN_$ERA_$PROCESS_bbb_bin_$#") // this needs to have "_bbb_bin_" in the pattern for the mergeXbbb option to work
      .SetAddThreshold(0.)
      .SetMergeThreshold(0.5)
      .SetFixNorm(false);
      bbb_real.MergeBinErrors(cb.cp().backgrounds().process({"EmbedZTT"}));
      bbb_real.AddBinByBin(cb.cp().backgrounds().process({"EmbedZTT"}), cb);

      // we make sure the 2 largest backgrounds have seperate bbbs above. Now for the smaller backgrounds we merge all bbb uncertainties into a single nuisance

      auto bbb_others = ch::BinByBinFactory()
      .SetPattern("CMS_$ANALYSIS_$CHANNEL_$BIN_$ERA_$PROCESS_bbb_bin_$#") // this needs to have "_bbb_bin_" in the pattern for the mergeXbbb option to work
      .SetAddThreshold(0.)
      .SetMergeThreshold(1.0)
      .SetFixNorm(false);
      bbb_others.MergeBinErrors(cb.cp().backgrounds().process({"jetFakes","EmbedZTT"}, false));
      bbb_others.AddBinByBin(cb.cp().backgrounds().process({"jetFakes","EmbedZTT"},false), cb);

      ////add bbb uncertainties for the signal but as we use reweighted histograms for sm, ps and mm these should be correlated. will need to do something for WH and ZH when we have the samples
      auto bbb_ggh = ch::BinByBinFactory()
      .SetPattern("CMS_$ANALYSIS_$CHANNEL_$BIN_$ERA_ggH_bbb_bin_$#")
      .SetAddThreshold(0.0)
      .SetMergeThreshold(0.0)
      .SetFixNorm(false);
      bbb_ggh.AddBinByBin(cb.cp().signals().process(sig_procs["ggH"]),cb);

      auto bbb_qqh = ch::BinByBinFactory()
      .SetPattern("CMS_$ANALYSIS_$CHANNEL_$BIN_$ERA_qqH_bbb_bin_$#")
      .SetAddThreshold(0.0)
      .SetMergeThreshold(0.0)
      .SetFixNorm(false);
      bbb_qqh.AddBinByBin(cb.cp().signals().process({"qqH_sm_htt","qqH_ps_htt","qqH_mm_htt"}),cb);

// neglect VH uncerts as they are a small contribution

//      auto bbb_Wh = ch::BinByBinFactory()
//      .SetPattern("CMS_$ANALYSIS_$CHANNEL_$BIN_$ERA_WH_bbb_bin_$#")
//      .SetAddThreshold(0.0)
//      .SetMergeThreshold(0.0)
//      .SetFixNorm(false);
//      bbb_qqh.AddBinByBin(cb.cp().signals().process({"WH_sm_htt","WH_ps_htt","WH_mm_htt"}),cb);
//
//      auto bbb_Zh = ch::BinByBinFactory()
//      .SetPattern("CMS_$ANALYSIS_$CHANNEL_$BIN_$ERA_ZH_bbb_bin_$#")
//      .SetAddThreshold(0.0)
//      .SetMergeThreshold(0.0)
//      .SetFixNorm(false);
//      bbb_qqh.AddBinByBin(cb.cp().signals().process({"ZH_sm_htt","ZH_ps_htt","ZH_mm_htt"}),cb);

     //  if we merge the x-axis bins then we need to rename the bbb uncertainties so that they are correlated properly
     //
     //  First we will deal with the catogiries with flat background when all phi_CP bins are merged into 1 (i.e all categories except the mu+pi and pi+pi channels)
     //  need to hardcode the bin number for the xbins
     //  Each vector element i corresponds to the number of xbins for bin i+1 
     //  if these numbers aren't set correctly the method won't work so be careful!
     //  Note that the merging is now only performed for the EmbedZTT as this has a flat distribution
     std::vector<unsigned> mt_nxbins = {1,1,16,1,8,4}; // note setting element 3 to 1 because we dont want to merge bins for mu+pi channel!
     std::vector<unsigned> tt_nxbins = {1,1,16,4,8,4,16,1,4,4,4}; // note setting element 7 to 1 because we dont want to merge bins for pi+pi channel!

     tt_nxbins = {1,1,10,4,4,4,10,1,4,4,4}; // note setting element 7 to 1 because we dont want to merge bins for pi+pi channel!
     mt_nxbins = {1,1,10,1,4,4};



     for(auto year: years) {
       for (string ch: chns) {
         std::vector<unsigned> bins = {};
         if(ch == "tt") bins = tt_nxbins;
         else bins = mt_nxbins;
         for (unsigned i=0; i<bins.size(); ++i) {
           unsigned nxbins = bins[i];
           if (nxbins <=1) continue; 
           std::cout << "merging bins for " << ch+"_"+year << " channel for category " << i+1 << ", nxbins set to " << nxbins << std::endl; 
           cb.cp().backgrounds().process({"jetFakes"}, false).channel({ch+"_"+year}).bin_id({(int)i+1}).ForEachProc([&](ch::Process *proc){
             TH1D *nominal = (TH1D*)proc->ClonedShape().get()->Clone();
             cb.cp().ForEachSyst([&](ch::Systematic *syst) {
               auto old_name = syst->name();
               std::string nonum_name = old_name;
               bool match_proc = (MatchingProcess(*proc,*syst)); 
               if (match_proc && old_name.find("_bbb_bin_") != std::string::npos) {
                 int bin_num = -1;
                 std::stringstream old_name_ss;
                 old_name_ss << old_name;
                 string temp;
                 int found;
                 while (std::getline(old_name_ss, temp, '_')) {
                   if (stringstream(temp) >> found) bin_num = found;
                 }
                 if((bin_num-1) % nxbins==0 ) {
                   nonum_name.erase (nonum_name.end()-std::to_string(bin_num).length(), nonum_name.end());
                   TH1D *shape_u_new = (TH1D*)syst->ClonedShapeU().get()->Clone();
                   TH1D *shape_d_new = (TH1D*)syst->ClonedShapeD().get()->Clone();
                   shape_u_new->Add(nominal,-1);
                   shape_d_new->Add(nominal,-1);
                   std::vector<std::string> names = {};
                   for(unsigned j = bin_num+1; j<(unsigned)bin_num+nxbins; ++j) names.push_back(nonum_name+std::to_string(j)); 
                   cb.cp().syst_name(names).ForEachSyst([&](ch::Systematic *s) {
                     TH1D *shape_u_temp = (TH1D*)s->ClonedShapeU().get()->Clone();
                     TH1D *shape_d_temp = (TH1D*)s->ClonedShapeD().get()->Clone();
                     shape_u_temp->Add(nominal,-1);
                     shape_d_temp->Add(nominal,-1);
                     shape_u_new->Add(shape_u_temp);
                     shape_d_new->Add(shape_d_temp);
                   });
                   shape_u_new->Add(nominal);
                   shape_d_new->Add(nominal);
                   syst->set_shapes(std::unique_ptr<TH1>(static_cast<TH1*>(shape_u_new)),std::unique_ptr<TH1>(static_cast<TH1*>(shape_d_new)),nullptr);
                   syst->set_value_u((syst->value_u()-1.)*nxbins + 1.);
                   syst->set_value_d((syst->value_d()-1.)*nxbins + 1.); 
                   for (auto n : names) {
                     cb.FilterSysts([&](ch::Systematic *s){
                       return s->name() == n;
                     });
                   }
                 }
               }  
             });
           });
         }
       }
     }

     // now we want to merge the processes that aren't flat but that are symmetric about phiCP=pi

     tt_nxbins = {1,1,10,4,4,4,10,4,4,4,4};
     mt_nxbins = {1,1,10,8,4,4};
     

     for(auto year: years) { 
       for (string ch: chns) { 
         std::vector<unsigned> bins = {};
         if(ch == "tt") bins = tt_nxbins;
         else bins = mt_nxbins;
         for (unsigned i=0; i<bins.size(); ++i) {
           unsigned nxbins = bins[i];
           if (nxbins <=1) continue;
           std::cout << "merging bins for " << ch+"_"+year << " channel for category " << i+1 << ", nxbins set to " << nxbins << std::endl;
           
           auto procs = cb.cp().process(JoinStr({{"jetFakes"},sig_procs["ggH"],sig_procs["qqH"]})).channel({ch+"_"+year}).bin_id({(int)i+1}); //for all j->tau fake processes and signal
           if((ch == "tt" && i==7) || (ch == "mt" && i==3)) procs = cb.cp().channel({ch+"_"+year}).bin_id({(int)i+1}); //for pi+pi and mu+pi channels include all other processes as well

           //auto procs = cb.cp().backgrounds().process({"jetFakes"}).channel({ch+"_"+year}).bin_id({(int)i+1}); //for all j->tau fake processes and signal
           //if((ch == "tt" && i==7) || (ch == "mt" && i==3)) procs = cb.cp().backgrounds().channel({ch+"_"+year}).bin_id({(int)i+1}); //for pi+pi and mu+pi channels include all other processes as well

           procs.ForEachProc([&](ch::Process *proc){
             TH1D *nominal = (TH1D*)proc->ClonedShape().get()->Clone();
             cb.cp().ForEachSyst([&](ch::Systematic *syst) {
               auto old_name = syst->name();
               std::string nonum_name = old_name;
               bool match_proc = (MatchingProcess(*proc,*syst));
               if (match_proc && old_name.find("_bbb_bin_") != std::string::npos) {
                 int bin_num = -1; 
                 std::stringstream old_name_ss;
                 old_name_ss << old_name;
                 string temp;
                 int found;
                 while (std::getline(old_name_ss, temp, '_')) {
                   if (stringstream(temp) >> found) bin_num = found;
                 }
                 int bin_num_y =floor((double)(bin_num-1)/(double)nxbins);
                 if((bin_num-bin_num_y*nxbins)<=nxbins/2) {
                 int bin_num_hi = (bin_num_y+1)*nxbins - (bin_num-bin_num_y*nxbins) + 1;
                   nonum_name.erase (nonum_name.end()-std::to_string(bin_num).length(), nonum_name.end());
                   TH1D *shape_u_new = (TH1D*)syst->ClonedShapeU().get()->Clone();
                   TH1D *shape_d_new = (TH1D*)syst->ClonedShapeD().get()->Clone();
                   shape_u_new->Add(nominal,-1);
                   shape_d_new->Add(nominal,-1);
                   
                   std::string to_add_name = nonum_name+std::to_string(bin_num_hi);
                   cb.cp().syst_name({to_add_name}).ForEachSyst([&](ch::Systematic *s) {
                     bool match_proc_2 = (MatchingProcess(*proc,*s));
                     if(match_proc_2){
                       TH1D *shape_u_temp = (TH1D*)s->ClonedShapeU().get()->Clone();
                       TH1D *shape_d_temp = (TH1D*)s->ClonedShapeD().get()->Clone();
                       shape_u_temp->Add(nominal,-1);
                       shape_d_temp->Add(nominal,-1);
                       shape_u_new->Add(shape_u_temp);
                       shape_d_new->Add(shape_d_temp); 
                     }
                   });
                   shape_u_new->Add(nominal);
                   shape_d_new->Add(nominal);
                   syst->set_shapes(std::unique_ptr<TH1>(static_cast<TH1*>(shape_u_new)),std::unique_ptr<TH1>(static_cast<TH1*>(shape_d_new)),nullptr);
                   syst->set_value_u((syst->value_u()-1.)*2 + 1.);
                   syst->set_value_d((syst->value_d()-1.)*2 + 1.);
                   std::cout << "removing" << "    " << to_add_name << std::endl;
                   cb.FilterSysts([&](ch::Systematic *s){
                     bool match = (MatchingProcess(*proc,*s));
                     return s->name() == to_add_name && match;
                   });
                 }
               }
             });
           });

         }
       }
     } 

    }


    cb.ForEachSyst([](ch::Systematic *s) {
        if (s->type().find("shape") == std::string::npos) return;
        if(!(s->value_d()<0.001 || s->value_u()<0.001)) return;
        std::cout << "[Negative yield] Fixing negative yield for syst" << s->bin()
                 << "," << s->process() << "," << s->name() << "\n";
        if(s->value_u()<0.001){
           s->set_value_u(0.001);
           s->set_type("lnN");
        }
        if(s->value_d()<0.001){
           s->set_value_d(0.001);
           s->set_type("lnN");
        }
    });

    // in this part of the code we rename the theory uncertainties for the VBF process so that they are not correlated with the ggH ones
    cb.cp().process({"qqH_sm_htt","qqH_ps_htt","qqH_mm_htt"}).RenameSystematic(cb,"CMS_scale_gg_13TeV","QCDscale_qqH_ACCEPT");
    cb.cp().process({"ggH_sm_htt","ggH_ps_htt","ggH_mm_htt"}).RenameSystematic(cb,"CMS_scale_gg_13TeV","QCDscale_ggH_ACCEPT");
    cb.cp().process({"qqH_sm_htt","qqH_ps_htt","qqH_mm_htt"}).RenameSystematic(cb,"CMS_PS_FSR_ggH_13TeV","CMS_PS_FSR_VBF_13TeV");
    cb.cp().process({"qqH_sm_htt","qqH_ps_htt","qqH_mm_htt"}).RenameSystematic(cb,"CMS_PS_ISR_ggH_13TeV","CMS_PS_ISR_VBF_13TeV");

    // scale up/down QCD scale uncertainties to ensure they do not change the inclusive yields only the shapes/acceptance

    cb.cp().syst_name({"QCDscale_ggH_ACCEPT"}).channel({"et","et_2016","em","em_2016","mt","mt_2016","tt","tt_2016"}).ForEachSyst([](ch::Systematic *syst) {
        syst->set_value_u(syst->value_u()*1.16021);
        syst->set_value_d(syst->value_d()*0.847445);
    });
    cb.cp().syst_name({"QCDscale_qqH_ACCEPT"}).channel({"et","et_2016","em","em_2016","mt","mt_2016","tt","tt_2016"}).ForEachSyst([](ch::Systematic *syst) {
        syst->set_value_u(syst->value_u()*0.994194);
        syst->set_value_d(syst->value_d()*1.00908);
    });

    cb.cp().syst_name({"QCDscale_ggH_ACCEPT"}).channel({"et_2017","et_2018","em_2017","em_2018","mt_2017","mt_2018","tt_2017","tt_2018"}).ForEachSyst([](ch::Systematic *syst) {
        syst->set_value_u(syst->value_u()*1.15977);
        syst->set_value_d(syst->value_d()*0.848289);
    });
    cb.cp().syst_name({"QCDscale_qqH_ACCEPT"}).channel({"et_2017","et_2018","em_2017","em_2018","mt_2017","mt_2018","tt_2017","tt_2018"}).ForEachSyst([](ch::Systematic *syst) {
        syst->set_value_u(syst->value_u()*0.995378);
        syst->set_value_d(syst->value_d()*1.00768);
    });

    // this part of the code should be used to handle the propper correlations between MC and embedded uncertainties and renaming of systematics to match Higgs comb requirements - so no need to try and implement any different treatments in HttSystematics_SMRun2 
    cb.cp().RenameSystematic(cb,"CMS_PreFire_13TeV","CMS_prefiring"); 
 
    // partially decorrelate the energy scale uncertainties
    cb.cp().RenameSystematic(cb,"CMS_scale_e_13TeV","CMS_scale_e");
    DecorrelateMCAndEMB(cb,"CMS_scale_e","CMS_scale_embedded_e",0.5);
    cb.cp().RenameSystematic(cb,"CMS_scale_mu_13TeV","CMS_scale_m");
    DecorrelateMCAndEMB(cb,"CMS_scale_m","CMS_scale_embedded_m",0.5);
    DecorrelateMCAndEMB(cb,"CMS_scale_t_1prong_13TeV","CMS_scale_embedded_t_1prong_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_scale_t_1prong1pizero_13TeV","CMS_scale_embedded_t_1prong1pizero_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_scale_t_3prong_13TeV","CMS_scale_embedded_t_3prong_13TeV",0.5);
    // partially decorrelate the ID uncertainties uncertainties
    DecorrelateMCAndEMB(cb,"CMS_eff_m","CMS_eff_embedded_m",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_e","CMS_eff_embedded_e",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_t_mt_13TeV","CMS_eff_embedded_t_mt_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_t_et_13TeV","CMS_eff_embedded_t_et_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_t_tt_13TeV","CMS_eff_embedded_t_tt_13TeV",0.5);

    DecorrelateMCAndEMB(cb,"CMS_eff_t_DM0_13TeV","CMS_eff_embedded_t_DM0_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_t_DM1_13TeV","CMS_eff_embedded_t_DM1_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_t_DM10_13TeV","CMS_eff_embedded_t_DM10_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_t_DM11_13TeV","CMS_eff_embedded_t_DM11_13TeV",0.5);
  
    DecorrelateMCAndEMB(cb,"CMS_eff_t_bin1_13TeV","CMS_eff_embedded_t_bin1_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_t_bin2_13TeV","CMS_eff_embedded_t_bin2_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_t_bin3_13TeV","CMS_eff_embedded_t_bin3_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_t_bin4_13TeV","CMS_eff_embedded_t_bin4_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_t_bin5_13TeV","CMS_eff_embedded_t_bin5_13TeV",0.5);
 
    DecorrelateMCAndEMB(cb,"CMS_eff_t_pTlow_MVADM0_13TeV","CMS_eff_embedded_t_pTlow_MVADM0_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_t_pTlow_MVADM1_13TeV","CMS_eff_embedded_t_pTlow_MVADM1_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_t_pTlow_MVADM2_13TeV","CMS_eff_embedded_t_pTlow_MVADM2_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_t_pTlow_MVADM10_13TeV","CMS_eff_embedded_t_pTlow_MVADM10_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_t_pTlow_MVADM11_13TeV","CMS_eff_embedded_t_pTlow_MVADM11_13TeV",0.5);

    DecorrelateMCAndEMB(cb,"CMS_eff_t_pThigh_MVADM0_13TeV","CMS_eff_embedded_t_pThigh_MVADM0_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_t_pThigh_MVADM1_13TeV","CMS_eff_embedded_t_pThigh_MVADM1_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_t_pThigh_MVADM2_13TeV","CMS_eff_embedded_t_pThigh_MVADM2_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_t_pThigh_MVADM10_13TeV","CMS_eff_embedded_t_pThigh_MVADM10_13TeV",0.5);
    DecorrelateMCAndEMB(cb,"CMS_eff_t_pThigh_MVADM11_13TeV","CMS_eff_embedded_t_pThigh_MVADM11_13TeV",0.5);
 
    // fully decorrelate lepton+tau trigger uncertainties for embedded and MC
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_Xtrigger_mt_DM0_13TeV","CMS_eff_embedded_Xtrigger_mt_DM0_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_Xtrigger_mt_DM1_13TeV","CMS_eff_embedded_Xtrigger_mt_DM1_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_Xtrigger_mt_DM10_13TeV","CMS_eff_embedded_Xtrigger_mt_DM10_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_Xtrigger_mt_DM11_13TeV","CMS_eff_embedded_Xtrigger_mt_DM11_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_Xtrigger_et_DM0_13TeV","CMS_eff_embedded_Xtrigger_et_DM0_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_Xtrigger_et_DM1_13TeV","CMS_eff_embedded_Xtrigger_et_DM1_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_Xtrigger_et_DM10_13TeV","CMS_eff_embedded_Xtrigger_et_DM10_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_Xtrigger_et_DM11_13TeV","CMS_eff_embedded_Xtrigger_et_DM11_13TeV"); 
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_t_trg_DM0_13TeV","CMS_eff_embedded_t_trg_DM0_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_t_trg_DM1_13TeV","CMS_eff_embedded_t_trg_DM1_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_t_trg_DM10_13TeV","CMS_eff_embedded_t_trg_DM10_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_t_trg_DM11_13TeV","CMS_eff_embedded_t_trg_DM11_13TeV");

    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_Xtrigger_mt_MVADM0_13TeV" ,"CMS_eff_embedded_Xtrigger_mt_MVADM0_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_Xtrigger_mt_MVADM1_13TeV" ,"CMS_eff_embedded_Xtrigger_mt_MVADM1_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_Xtrigger_mt_MVADM2_13TeV" ,"CMS_eff_embedded_Xtrigger_mt_MVADM2_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_Xtrigger_mt_MVADM10_13TeV","CMS_eff_embedded_Xtrigger_mt_MVADM10_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_Xtrigger_mt_MVADM11_13TeV","CMS_eff_embedded_Xtrigger_mt_MVADM11_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_Xtrigger_et_MVADM0_13TeV" ,"CMS_eff_embedded_Xtrigger_et_MVADM0_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_Xtrigger_et_MVADM1_13TeV" ,"CMS_eff_embedded_Xtrigger_et_MVADM1_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_Xtrigger_et_MVADM2_13TeV" ,"CMS_eff_embedded_Xtrigger_et_MVADM2_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_Xtrigger_et_MVADM10_13TeV","CMS_eff_embedded_Xtrigger_et_MVADM10_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_Xtrigger_et_MVADM11_13TeV","CMS_eff_embedded_Xtrigger_et_MVADM11_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_t_trg_MVADM0_13TeV"       ,"CMS_eff_embedded_t_trg_MVADM0_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_t_trg_MVADM1_13TeV"       ,"CMS_eff_embedded_t_trg_MVADM1_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_t_trg_MVADM2_13TeV"       ,"CMS_eff_embedded_t_trg_MVADM2_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_t_trg_MVADM10_13TeV"      ,"CMS_eff_embedded_t_trg_MVADM10_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_t_trg_MVADM11_13TeV"      ,"CMS_eff_embedded_t_trg_MVADM11_13TeV");
  
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_trigger_mt_13TeV","CMS_eff_embedded_trigger_mt_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_trigger_et_13TeV","CMS_eff_embedded_trigger_et_13TeV");
    cb.cp().process({"EmbedZTT"}).RenameSystematic(cb,"CMS_eff_trigger_em_13TeV","CMS_eff_embedded_trigger_em_13TeV");

    // de-correlate systematics for 2016 and 2017, ADD 2018 
    if((era.find("2016") != std::string::npos && era.find("2017") != std::string::npos && era.find("2018") != std::string::npos) ||  era.find("all") != std::string::npos || true){
      std::cout << "Partially Decorrelating systematics for 2016/2017/2018" << std::endl;
      Json::Value js;
      string json_file = string(getenv("CMSSW_BASE")) + "/src/CombineHarvester/HTTSMCPDecays18/scripts/correlations.json";
      js = ch::ExtractJsonFromFile(json_file);
      std::vector<std::string> keys = js.getMemberNames();
      for (std::vector<std::string>::const_iterator it = keys.begin(); it != keys.end(); ++it){
        string name = *it;
        double value = js[*it].asDouble();
        std::vector<string> chans_2016 = {"em","em_2016","et","et_2016","mt","mt_2016","tt","tt_2016","ttbar","ttbar_2016"};
        std::vector<string> chans_2017 = {"em_2017","et_2017","mt_2017","tt_2017","ttbar_2017"};
        std::vector<string> chans_2018 = {"em_2018","et_2018","mt_2018","tt_2018","ttbar_2018"};
        DecorrelateSyst (cb, name, value, chans_2016, chans_2017, chans_2018);
      }

      // now take care of cases where correlations are different for the 3 years
      string json_file_byyear = string(getenv("CMSSW_BASE")) + "/src/CombineHarvester/HTTSMCP2016/scripts/correlations_byyear.json";
      js = ch::ExtractJsonFromFile(json_file_byyear);
      keys = js.getMemberNames();
      for (std::vector<std::string>::const_iterator it = keys.begin(); it != keys.end(); ++it){
        string name = *it;
        std::vector<double> values = {js[*it]["2016"].asDouble(),js[*it]["2017"].asDouble(), js[*it]["2018"].asDouble()};
        std::vector<string> chans_2016 = {"em","em_2016","et","et_2016","mt","mt_2016","tt","tt_2016","ttbar","ttbar_2016"};
        std::vector<string> chans_2017 = {"em_2017","et_2017","mt_2017","tt_2017","ttbar_2017"};
        std::vector<string> chans_2018 = {"em_2018","et_2018","mt_2018","tt_2018","ttbar_2018"};
        DecorrelateSystSeperateYears (cb, name, values, chans_2016, chans_2017, chans_2018);
      }

    }

    // remove the 13TeV labelling from all uncerts except lumi
    Remove13TeVFromNames(cb);

    // if lnN uncertainties have no effect then remove
    cb.FilterSysts([&](ch::Systematic *s){
      bool filter = s->type().find("lnN") != std::string::npos && ((s->asymm() && s->value_u()==1 && s->value_d()==1) || (!s->asymm() && s->value_u()==1));
      //if (filter) {
      //  std::cout << "Filtering syst " << s->name() << "    " << s->type() << "    " << s->asymm() << "    " << s->value_u() << "    " <<  s->value_d() << std::endl;
      //std::cout << s->Systematic::PrintHeader << *s << std::endl; 
      //}   
      return filter;
    });

     ch::SetStandardBinNames(cb);
	//! [part8]

	
     // add autoMCStats options
     if(!mergeXbbb) cb.AddDatacardLineAtEnd("* autoMCStats 10 1"); 
     //
     // POISSON CUTOFF SHOULD BE 10 ?
     //
     //if(!mergeXbbb) cb.AddDatacardLineAtEnd("* autoMCStats 0 1");
     // add lumi_scale for projection scans
     //cb.AddDatacardLineAtEnd("lumi_scale rateParam * *  1. [0,4]");
     //cb.AddDatacardLineAtEnd("nuisance edit freeze lumi_scale");

     //! [part9]
     // First we generate a set of bin names:
     
     
     //Write out datacards. Naming convention important for rest of workflow. We
     //make one directory per chn-cat, one per chn and cmb. In this code we only
     //store the individual datacards for each directory to be combined later, but
     //note that it's also possible to write out the full combined card with CH
     string output_prefix = "output/";
     if(output_folder.compare(0,1,"/") == 0) output_prefix="";
     ch::CardWriter writer(output_prefix + output_folder + "/$TAG/$MASS/$BIN.txt",
         	    output_prefix + output_folder + "/$TAG/common/htt_input.root");
     
     
     writer.WriteCards("cmb", cb);
     
     writer.WriteCards("htt_2016", cb.cp().channel({"em_2016","et_2016","mt_2016","tt_2016","ttbar_2016"}));
     writer.WriteCards("htt_2017", cb.cp().channel({"em_2017","et_2017","mt_2017","tt_2017","ttbar_2017"})); 
     writer.WriteCards("htt_2018", cb.cp().channel({"em_2018","et_2018","mt_2018","tt_2018","ttbar_2018"}));

     writer.WriteCards("htt_tt", cb.cp().channel({"tt_2016","tt_2018","tt_2017"}));
     writer.WriteCards("htt_mt", cb.cp().channel({"mt_2016","mt_2018","mt_2017"}));
     
     for (auto chn : cb.channel_set()) {
     
       // per-channel
       writer.WriteCards(chn, cb.cp().channel({chn}));
       // And per-channel-category
     }
     writer.WriteCards("htt_tt_1_13TeV", cb.cp().channel({"tt_2016","tt_2017","tt_2018"}).bin_id({1}));
     writer.WriteCards("htt_tt_2_13TeV", cb.cp().channel({"tt_2016","tt_2017","tt_2018"}).bin_id({2}));
     writer.WriteCards("htt_tt_3_13TeV", cb.cp().channel({"tt_2016","tt_2017","tt_2018"}).bin_id({1,2,3}));
     writer.WriteCards("htt_tt_4_13TeV", cb.cp().channel({"tt_2016","tt_2017","tt_2018"}).bin_id({1,2,4}));
     writer.WriteCards("htt_tt_5_13TeV", cb.cp().channel({"tt_2016","tt_2017","tt_2018"}).bin_id({1,2,5}));
     writer.WriteCards("htt_tt_6_13TeV", cb.cp().channel({"tt_2016","tt_2017","tt_2018"}).bin_id({1,2,6}));
     writer.WriteCards("htt_tt_7_13TeV", cb.cp().channel({"tt_2016","tt_2017","tt_2018"}).bin_id({1,2,7}));
     writer.WriteCards("htt_tt_8_13TeV", cb.cp().channel({"tt_2016","tt_2017","tt_2018"}).bin_id({1,2,8}));
     writer.WriteCards("htt_tt_9_13TeV", cb.cp().channel({"tt_2016","tt_2017","tt_2018"}).bin_id({1,2,9}));
     writer.WriteCards("htt_tt_10_13TeV", cb.cp().channel({"tt_2016","tt_2017","tt_2018"}).bin_id({1,2,10}));   
     writer.WriteCards("htt_tt_11_13TeV", cb.cp().channel({"tt_2016","tt_2017","tt_2018"}).bin_id({1,2,11}));
     writer.WriteCards("htt_mt_1_13TeV", cb.cp().channel({"mt_2016","mt_2017","mt_2018"}).bin_id({1}));
     writer.WriteCards("htt_mt_2_13TeV", cb.cp().channel({"mt_2016","mt_2017","mt_2018"}).bin_id({2}));
     writer.WriteCards("htt_mt_3_13TeV", cb.cp().channel({"mt_2016","mt_2017","mt_2018"}).bin_id({1,2,3}));
     writer.WriteCards("htt_mt_4_13TeV", cb.cp().channel({"mt_2016","mt_2017","mt_2018"}).bin_id({1,2,4}));
     writer.WriteCards("htt_mt_5_13TeV", cb.cp().channel({"mt_2016","mt_2017","mt_2018"}).bin_id({1,2,5}));
     writer.WriteCards("htt_mt_6_13TeV", cb.cp().channel({"mt_2016","mt_2017","mt_2018"}).bin_id({1,2,6}));

     for(auto year: years) {
       writer.WriteCards("htt_tt_1_"+year+"_13TeV", cb.cp().channel({"tt_"+year}).bin_id({1}));
       writer.WriteCards("htt_tt_2_"+year+"_13TeV", cb.cp().channel({"tt_"+year}).bin_id({2}));
       writer.WriteCards("htt_tt_3_"+year+"_13TeV", cb.cp().channel({"tt_"+year}).bin_id({3}));
       writer.WriteCards("htt_tt_4_"+year+"_13TeV", cb.cp().channel({"tt_"+year}).bin_id({4}));
       writer.WriteCards("htt_tt_5_"+year+"_13TeV", cb.cp().channel({"tt_"+year}).bin_id({5}));
       writer.WriteCards("htt_tt_6_"+year+"_13TeV", cb.cp().channel({"tt_"+year}).bin_id({6}));
       writer.WriteCards("htt_tt_7_"+year+"_13TeV", cb.cp().channel({"tt_"+year}).bin_id({7}));
       writer.WriteCards("htt_tt_8_"+year+"_13TeV", cb.cp().channel({"tt_"+year}).bin_id({8}));
       writer.WriteCards("htt_tt_9_"+year+"_13TeV", cb.cp().channel({"tt_"+year}).bin_id({9}));
       writer.WriteCards("htt_tt_10_"+year+"13TeV", cb.cp().channel({"tt_"+year}).bin_id({10}));
       writer.WriteCards("htt_tt_11_"+year+"13TeV", cb.cp().channel({"tt_"+year}).bin_id({11}));
       writer.WriteCards("htt_mt_1_"+year+"_13TeV", cb.cp().channel({"mt_"+year}).bin_id({1}));
       writer.WriteCards("htt_mt_2_"+year+"_13TeV", cb.cp().channel({"mt_"+year}).bin_id({2}));
       writer.WriteCards("htt_mt_3_"+year+"_13TeV", cb.cp().channel({"mt_"+year}).bin_id({3}));
       writer.WriteCards("htt_mt_4_"+year+"_13TeV", cb.cp().channel({"mt_"+year}).bin_id({4}));
       writer.WriteCards("htt_mt_5_"+year+"_13TeV", cb.cp().channel({"mt_"+year}).bin_id({5}));
       writer.WriteCards("htt_mt_6_"+year+"_13TeV", cb.cp().channel({"mt_"+year}).bin_id({6}));

     }

     //writer.WriteCards("htt_mt_allWith5_13TeV", cb.cp().channel({"mt_2016","mt_2017","mt_2018"}).bin_id({1,2,3,4,5}));
     //writer.WriteCards("htt_mt_allWith6_13TeV", cb.cp().channel({"mt_2016","mt_2017","mt_2018"}).bin_id({1,2,3,4,6}));

     //writer.WriteCards("htt_mt_2017_3_13TeV", cb.cp().channel({"mt_2017"}).bin_id({1,2,3}));
     //writer.WriteCards("htt_mt_2017_4_13TeV", cb.cp().channel({"mt_2017"}).bin_id({1,2,4}));
     //writer.WriteCards("htt_mt_2017_5_13TeV", cb.cp().channel({"mt_2017"}).bin_id({1,2,5}));
     //writer.WriteCards("htt_mt_2018_3_13TeV", cb.cp().channel({"mt_2018"}).bin_id({1,2,3}));
     //writer.WriteCards("htt_mt_2018_4_13TeV", cb.cp().channel({"mt_2018"}).bin_id({1,2,4}));
     //writer.WriteCards("htt_mt_2018_5_13TeV", cb.cp().channel({"mt_2018"}).bin_id({1,2,5}));

     writer.WriteCards("htt_mt_mupi_13TeV", cb.cp().channel({"mt_2016","mt_2017","mt_2018"}).bin_id({1,2,3}));
     writer.WriteCards("htt_mt_murho_13TeV", cb.cp().channel({"mt_2016","mt_2017","mt_2018"}).bin_id({4,5,6}));
     writer.WriteCards("htt_mt_mua1_13TeV", cb.cp().channel({"mt_2016","mt_2017","mt_2018"}).bin_id({7,8,9}));
     writer.WriteCards("htt_mt_Combined_13TeV", cb.cp().channel({"mt_2016","mt_2017","mt_2018"}).bin_id({1,2,3,4,5,6,7,8,9}));
        
     writer.WriteCards("htt_bkg", cb.cp().bin_id({1,2}));

     cb.cp().channel({"tt_2016","tt_2017","tt_2018"}).bin_id({3,7}, false).ForEachObj([&](ch::Object *obj){
         obj->set_attribute("cat","stage2");
      });
     cb.cp().channel({"tt_2016","tt_2017","tt_2018"}, false).bin_id({3,4}, false).ForEachObj([&](ch::Object *obj){
         obj->set_attribute("cat","stage2");
      });
    writer.WriteCards("htt_stage2", cb.cp().attr({"stage2"},"cat"));

    cb.PrintAll();
    cout << " done\n";
    
    
}
