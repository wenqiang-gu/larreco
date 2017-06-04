#ifndef GAUSHITFINDER_H
#define GAUSHITFINDER_H

////////////////////////////////////////////////////////////////////////
//
// GaussHitFinder class
//
// jaasaadi@syr.edu
//
//  This algorithm is designed to find hits on wires after deconvolution.
// -----------------------------------
// This algorithm is based on the FFTHitFinder written by Brian Page, 
// Michigan State University, for the ArgoNeuT experiment.
// 
//
// The algorithm walks along the wire and looks for pulses above threshold
// The algorithm then attempts to fit n-gaussians to these pulses where n
// is set by the number of peaks found in the pulse
// If the Chi2/NDF returned is "bad" it attempts to fit n+1 gaussians to
// the pulse. If this is a better fit it then uses the parameters of the 
// Gaussian fit to characterize the "hit" object 
//
// To use this simply include the following in your producers:
// gaushit:     @local::microboone_gaushitfinder
// gaushit:	@local::argoneut_gaushitfinder
////////////////////////////////////////////////////////////////////////


// C/C++ standard library
#include <algorithm> // std::accumulate()
#include <vector>
#include <string>
#include <memory> // std::unique_ptr()
#include <utility> // std::move()

// Framework includes
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Core/EDProducer.h"
#include "canvas/Persistency/Common/FindOneP.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Services/Optional/TFileService.h"
#include "fhiclcpp/ParameterSet.h"
#include "art/Utilities/ToolMacros.h"
#include "art/Utilities/make_tool.h"

// LArSoft Includes
#include "larcoreobj/SimpleTypesAndConstants/RawTypes.h" // raw::ChannelID_t
#include "larcore/Geometry/Geometry.h"
#include "larcore/Geometry/CryostatGeo.h"
#include "larcore/Geometry/TPCGeo.h"
#include "larcore/Geometry/PlaneGeo.h"
#include "lardataobj/RecoBase/Wire.h"
#include "lardataobj/RecoBase/Hit.h"
#include "lardata/ArtDataHelper/HitCreator.h"
#include "HitFilterAlg.h"

#include "larreco/HitFinder/HitFinderTools/ICandidateHitFinder.h"
#include "larreco/HitFinder/HitFinderTools/IPeakFitter.h"

// ROOT Includes
#include "TGraphErrors.h"
#include "TH1D.h"
#include "TDecompSVD.h"
#include "TMath.h"
#include "TF1.h"
#include "TTree.h"
#include "TStopwatch.h"

namespace hit{
class GausHitFinder : public art::EDProducer {
  
public:
  
    explicit GausHitFinder(fhicl::ParameterSet const& pset);
       
    void produce(art::Event& evt) override;
    void beginJob() override;
    void endJob() override;
    void reconfigure(fhicl::ParameterSet const& p) override;

private:
  
    void FillOutHitParameterVector(const std::vector<double>& input, std::vector<double>& output);
    
    double              fThreshold          = 0.;  // minimum signal size for id'ing a hit
    double              fMinWidth		    = 0 ;  // hit minimum width
    std::string         fCalDataModuleLabel;

    std::vector<double> fMinSigVec;                ///<signal height threshold
    std::vector<double> fMinWidthVec;              ///<Minimum hit width
    std::vector<int>    fLongMaxHitsVec;           ///<Maximum number hits on a really long pulse train
    std::vector<int>    fLongPulseWidthVec;        ///<Sets width of hits used to describe long pulses
  
    size_t              fMaxMultiHit;              ///<maximum hits for multi fit
    int                 fAreaMethod;               ///<Type of area calculation
    std::vector<double> fAreaNormsVec;             ///<factors for converting area to same units as peak height
    bool	            fTryNplus1Fits;            ///<whether we will (true) or won't (false) try n+1 fits
    double	            fChi2NDFRetry;             ///<Value at which a second n+1 Fit will be tried
    double	            fChi2NDF;                  ///maximum Chisquared / NDF allowed for a hit to be saved
    size_t              fNumBinsToAverage;         ///< If bin averaging for peak finding, number bins to average
    
    std::unique_ptr<reco_tool::ICandidateHitFinder> fHitFinderTool;  ///< For finding candidate hits
    std::unique_ptr<reco_tool::IPeakFitter>         fPeakFitterTool; ///< Perform fit to candidate peaks
    std::unique_ptr<HitFilterAlg>                   fHitFilterAlg;   ///< algorithm used to filter out noise hits
  
    TH1F* fFirstChi2;
    TH1F* fChi2;
    
}; // class GausHitFinder
  

//-------------------------------------------------
//-------------------------------------------------
GausHitFinder::GausHitFinder(fhicl::ParameterSet const& pset)
{
    this->reconfigure(pset);
  
    // let HitCollectionCreator declare that we are going to produce
    // hits and associations with wires and raw digits
    // (with no particular product label)
    recob::HitCollectionCreator::declare_products(*this);
  
} // GausHitFinder::GausHitFinder()


//-------------------------------------------------
//-------------------------------------------------
void GausHitFinder::FillOutHitParameterVector(const std::vector<double>& input,
                                              std::vector<double>&       output)
{
    if(input.size()==0)
        throw std::runtime_error("GausHitFinder::FillOutHitParameterVector ERROR! Input config vector has zero size.");

    art::ServiceHandle<geo::Geometry> geom;
    const unsigned int N_PLANES = geom->Nplanes();

    if(input.size()==1)
        output.resize(N_PLANES,input[0]);
    else if(input.size()==N_PLANES)
        output = input;
    else
        throw std::runtime_error("GausHitFinder::FillOutHitParameterVector ERROR! Input config vector size !=1 and !=N_PLANES.");
      
}
						
  
//-------------------------------------------------
//-------------------------------------------------
void GausHitFinder::reconfigure(fhicl::ParameterSet const& p)
{
    // Implementation of optional member function here.
    fCalDataModuleLabel = p.get< std::string  >("CalDataModuleLabel");
  
    
    bool const doHitFiltering = p.get<bool>("FilterHits", false);
    if (doHitFiltering) {
      if (fHitFilterAlg) { // create a new algorithm instance
        fHitFilterAlg->reconfigure(p.get<fhicl::ParameterSet>("HitFilterAlg"));
      }
      else { // reconfigure the existing instance
        fHitFilterAlg = std::make_unique<HitFilterAlg>
          (p.get<fhicl::ParameterSet>("HitFilterAlg"));
      }
    }

    FillOutHitParameterVector(p.get< std::vector<double> >("MinSig"),         fMinSigVec);
    FillOutHitParameterVector(p.get< std::vector<double> >("MinWidth"),       fMinWidthVec);
    FillOutHitParameterVector(p.get< std::vector<double> >("AreaNorms"),      fAreaNormsVec);

    fLongMaxHitsVec    = p.get< std::vector<int>>("LongMaxHits",    std::vector<int>() = {25,25,25});
    fLongPulseWidthVec = p.get< std::vector<int>>("LongPulseWidth", std::vector<int>() = {16,16,16});
    fMaxMultiHit       = p.get< int             >("MaxMultiHit");
    fAreaMethod        = p.get< int             >("AreaMethod");
    fTryNplus1Fits     = p.get< bool            >("TryNplus1Fits");
    fChi2NDFRetry      = p.get< double          >("Chi2NDFRetry");
    fChi2NDF           = p.get< double          >("Chi2NDF");
    fNumBinsToAverage  = p.get< size_t          >("NumBinsToAverage", 0);
    
    // recover the tool to do the candidate hit finding
    // Recover the baseline tool
    fHitFinderTool  = art::make_tool<reco_tool::ICandidateHitFinder>(p.get<fhicl::ParameterSet>("CandidateHits"));
    fPeakFitterTool = art::make_tool<reco_tool::IPeakFitter>(p.get<fhicl::ParameterSet>("PeakFitter"));

    return;
}  

//-------------------------------------------------
//-------------------------------------------------
void GausHitFinder::beginJob()
{
    // get access to the TFile service
    art::ServiceHandle<art::TFileService> tfs;
   
    
    // ======================================
    // === Hit Information for Histograms ===
    fFirstChi2	= tfs->make<TH1F>("fFirstChi2", "#chi^{2}", 10000, 0, 5000);
    fChi2	        = tfs->make<TH1F>("fChi2", "#chi^{2}", 10000, 0, 5000);
}

//-------------------------------------------------
//-------------------------------------------------
void GausHitFinder::endJob()
{

}

//  This algorithm uses the fact that deconvolved signals are very smooth 
//  and looks for hits as areas between local minima that have signal above 
//  threshold.
//-------------------------------------------------
void GausHitFinder::produce(art::Event& evt)
{
    //==================================================================================================
   
    TH1::AddDirectory(kFALSE);
   
    // Instantiate and Reset a stop watch
    //TStopwatch StopWatch;
    //StopWatch.Reset();
   
    // ################################
    // ### Calling Geometry service ###
    // ################################
    art::ServiceHandle<geo::Geometry> geom;

    // ###############################################
    // ### Making a ptr vector to put on the event ###
    // ###############################################
    // this contains the hit collection
    // and its associations to wires and raw digits
    recob::HitCollectionCreator hcol(*this, evt);
   
    // ##########################################
    // ### Reading in the Wire List object(s) ###
    // ##########################################
    art::Handle< std::vector<recob::Wire> > wireVecHandle;
    evt.getByLabel(fCalDataModuleLabel,wireVecHandle);
   
    // #################################################################
    // ### Reading in the RawDigit associated with these wires, too  ###
    // #################################################################
    art::FindOneP<raw::RawDigit> RawDigits
        (wireVecHandle, evt, fCalDataModuleLabel);
   
    // Channel Number
    raw::ChannelID_t channel = raw::InvalidChannelID;
    
    //#################################################
    //###    Set the charge determination method    ###
    //### Default is to compute the normalized area ###
    //#################################################
    std::function<double (double,double,double,double,int,int)> chargeFunc = [](double peakMean, double peakAmp, double peakWidth, double areaNorm, int low, int hi){return std::sqrt(2*TMath::Pi())*peakAmp*peakWidth/areaNorm;};
    
    //##############################################
    //### Alternative is to integrate over pulse ###
    //##############################################
    if (fAreaMethod == 0)
        chargeFunc = [](double peakMean, double peakAmp, double peakWidth, double areaNorm, int low, int hi)
                        {
                            double charge(0);
                            for(int sigPos = low; sigPos < hi; sigPos++)
                                charge += peakAmp * TMath::Gaus(sigPos,peakMean,peakWidth);
                            return charge;
                        };
    
    //##############################
    //### Looping over the wires ###
    //##############################
    for(size_t wireIter = 0; wireIter < wireVecHandle->size(); wireIter++)
    {
        // ####################################
        // ### Getting this particular wire ###
        // ####################################
        art::Ptr<recob::Wire>   wire(wireVecHandle, wireIter);
        art::Ptr<raw::RawDigit> rawdigits = RawDigits.at(wireIter);
       
        // --- Setting Channel Number and Signal type ---
        channel = wire->Channel();
        
        // get the WireID for this hit
        std::vector<geo::WireID> wids = geom->ChannelToWire(channel);
        // for now, just take the first option returned from ChannelToWire
        geo::WireID wid  = wids[0];
        // We need to know the plane to look up parameters
        geo::PlaneID::PlaneID_t plane = wid.Plane;

        // ----------------------------------------------------------
        // -- Setting the appropriate signal widths and thresholds --
        // --    for the right plane.      --
        // ----------------------------------------------------------
        
        fThreshold = fMinSigVec.at(plane);
        fMinWidth  = fMinWidthVec.at(plane);
        
//            if (wid.Plane == geo::kV)
//                roiThreshold = std::max(threshold,std::min(2.*threshold,*std::max_element(signal.begin(),signal.end())/3.));

        // #################################################
        // ### Set up to loop over ROI's for this wire   ###
        // #################################################
        const recob::Wire::RegionsOfInterest_t& signalROI = wire->SignalROI();
       
        for(const auto& range : signalROI.get_ranges())
        {
            // #################################################
            // ### Getting a vector of signals for this wire ###
            // #################################################
            //std::vector<float> signal(wire->Signal());

            const std::vector<float>& signal = range.data();
      
            // ##########################################################
            // ### Making an iterator for the time ticks of this wire ###
            // ##########################################################
            std::vector<float>::const_iterator timeIter;  	    // iterator for time bins
           
            // ROI start time
            raw::TDCtick_t roiFirstBinTick = range.begin_index();
            float          roiThreshold(fThreshold);
            
            // ###########################################################
            // ### Scan the waveform and find candidate peaks + merge  ###
            // ###########################################################
            
            reco_tool::ICandidateHitFinder::HitCandidateVec      hitCandidateVec;
            reco_tool::ICandidateHitFinder::MergeHitCandidateVec mergedCandidateHitVec;
            
            fHitFinderTool->findHitCandidates(signal, 0, roiThreshold, hitCandidateVec);
            fHitFinderTool->MergeHitCandidates(signal, hitCandidateVec, mergedCandidateHitVec);
            
            // #######################################################
            // ### Lets loop over the pulses we found on this wire ###
            // #######################################################
            
            for(auto& mergedCands : mergedCandidateHitVec)
            {
                int startT= mergedCands.front().startTick;
                int endT  = mergedCands.back().stopTick;

                // ### Putting in a protection in case things went wrong ###
                // ### In the end, this primarily catches the case where ###
                // ### a fake pulse is at the start of the ROI           ###
                if (endT - startT < 5) continue;
	 
                // #######################################################
                // ### Clearing the parameter vector for the new pulse ###
                // #######################################################
	 
                // === Setting the number of Gaussians to try ===
                int nGausForFit = mergedCands.size();
	 
                // ##################################################
                // ### Calling the function for fitting Gaussians ###
                // ##################################################
                double                                chi2PerNDF(0.);
                int                                   NDF(0);
                reco_tool::IPeakFitter::PeakParamsVec peakParamsVec;
                
                // #######################################################
                // ### If # requested Gaussians is too large then punt ###
                // #######################################################
                if (mergedCands.size() <= fMaxMultiHit)
                {
                    fPeakFitterTool->findPeakParameters(signal, mergedCands, peakParamsVec, chi2PerNDF, NDF);
               
                    // If the chi2 is infinite then there is a real problem so we bail
                    if (!(chi2PerNDF < std::numeric_limits<double>::infinity())) continue;
                   
                    fFirstChi2->Fill(chi2PerNDF);
/*
                    // #######################################################
                    // ### Clearing the parameter vector for the new pulse ###
                    // #######################################################
                    double       chi2PerNDF2(0.);
                    int          NDF2(0);
                    ParameterVec paramVec2;
                
                    // #####################################################
                    // ### Trying extra gaussians for an initial bad fit ###
                    // #####################################################
                    if( (chi2PerNDF > (2*fChi2NDFRetry) && fTryNplus1Fits && nGausForFit == 1)||
                        (chi2PerNDF > (fChi2NDFRetry)   && fTryNplus1Fits && nGausForFit >  1))
                    {
                        // ############################################################
                        // ### Modify input parameters for re-fitting n+1 Gaussians ###
                        // ############################################################
                        int newPeakTime = peakVals[0].first + 5 * nGausForFit;
                    
                        // We need to make sure we are not out of range and new peak amplitude is non-negative
                        if (newPeakTime < endT - 1 && signal[newPeakTime] > 0.)
                        {
                            peakVals.emplace_back(newPeakTime, 2. * peakVals[0].second);
	    
                            // #########################################################
                            // ### Calling the function for re-fitting n+1 Gaussians ###
                            // #########################################################
                            FitGaussians(signal, peakVals, startT, endT, 0.5, paramVec2, chi2PerNDF2, NDF2);
	    
                            // #########################################################
                            // ### Getting the appropriate parameter into the vector ###
                            // #########################################################
                            if (chi2PerNDF2 < chi2PerNDF)
                            {
                                nGausForFit = peakVals.size();
                                chi2PerNDF  = chi2PerNDF2;
                                NDF         = NDF2;
                                paramVec    = paramVec2;
                            }
                        }
                    }
 */
                }
                
                // #######################################################
                // ### If too large then force alternate solution      ###
                // ### - Make n hits from pulse train where n will     ###
                // ###   depend on the fhicl parameter fLongPulseWidth ###
                // ### Also do this if chi^2 is too large              ###
                // #######################################################
                if (mergedCands.size() > fMaxMultiHit || chi2PerNDF > fChi2NDF)
                {
                    int longPulseWidth = fLongPulseWidthVec.at(plane);
                    int nHitsThisPulse = (endT - startT) / longPulseWidth;
                    
                    if (nHitsThisPulse > fLongMaxHitsVec.at(plane))
                    {
                        nHitsThisPulse = fLongMaxHitsVec.at(plane);
                        longPulseWidth = (endT - startT) / nHitsThisPulse;
                    }
                    
                    if (nHitsThisPulse * longPulseWidth < endT - startT) nHitsThisPulse++;
                    
                    int firstTick = startT;
                    int lastTick  = firstTick + std::min(endT,longPulseWidth);
                    
                    peakParamsVec.clear();
                    nGausForFit = nHitsThisPulse;
                    NDF         = 1.;
                    chi2PerNDF  =  chi2PerNDF > fChi2NDF ? chi2PerNDF : -1.;
                    
                    for(int hitIdx = 0; hitIdx < nHitsThisPulse; hitIdx++)
                    {
                        // This hit parameters
                        double sumADC    = std::accumulate(signal.begin() + firstTick, signal.begin() + lastTick, 0.);
                        double peakSigma = (lastTick - firstTick) / 3.;  // Set the width...
                        double peakAmp   = 0.3989 * sumADC / peakSigma;  // Use gaussian formulation
                        double peakMean  = (firstTick + lastTick) / 2.;
                    
                        // Store hit params
                        reco_tool::IPeakFitter::PeakFitParams_t peakParams;
                        
                        peakParams.peakCenter         = peakMean;
                        peakParams.peakCenterError    = 0.1 * peakMean;
                        peakParams.peakSigma          = peakSigma;
                        peakParams.peakSigmaError     = 0.1 * peakSigma;
                        peakParams.peakAmplitude      = peakAmp;
                        peakParams.peakAmplitudeError = 0.1 * peakAmp;
                        
                        peakParamsVec.push_back(peakParams);
                        
                        // set for next loop
                        firstTick = lastTick;
                        lastTick  = std::min(lastTick  + longPulseWidth, endT);
                    }
                }
	    
                // #######################################################
                // ### Loop through returned peaks and make recob hits ###
                // #######################################################
                
                int numHits(0);
                
                for(const auto& peakParams : peakParamsVec)
                {
                    // Extract values for this hit
                    double peakAmp   = peakParams.peakAmplitude;
                    double peakMean  = peakParams.peakCenter;
                    double peakWidth = peakParams.peakSigma;
                    
                    // Extract errors
                    double peakAmpErr   = peakParams.peakAmplitudeError;
                    double peakMeanErr  = peakParams.peakCenterError;
                    double peakWidthErr = peakParams.peakSigmaError;
                    
                    // ### Charge ###
                    //double charge    = chargeFunc(peakMean, peakAmp, peakWidth, fAreaNorms[view],startT,endT);;
                    double charge    = chargeFunc(peakMean, peakAmp, peakWidth, fAreaNormsVec[plane],startT,endT);;
                    double chargeErr = std::sqrt(TMath::Pi()) * (peakAmpErr*peakWidthErr + peakWidthErr*peakAmpErr);
                    
                    // ### limits for getting sums
                    std::vector<float>::const_iterator sumStartItr = signal.begin() + startT;
                    std::vector<float>::const_iterator sumEndItr   = signal.begin() + endT;
                    
                    // ### Sum of ADC counts
                    double sumADC = std::accumulate(sumStartItr, sumEndItr, 0.);

                    // ok, now create the hit
                    recob::HitCreator hitcreator(*wire,                            // wire reference
                                                 wid,                              // wire ID
                                                 startT+roiFirstBinTick,           // start_tick TODO check
                                                 endT+roiFirstBinTick,             // end_tick TODO check
                                                 peakWidth,                        // rms
                                                 peakMean+roiFirstBinTick,         // peak_time
                                                 peakMeanErr,                      // sigma_peak_time
                                                 peakAmp,                          // peak_amplitude
                                                 peakAmpErr,                       // sigma_peak_amplitude
                                                 charge,                           // hit_integral
                                                 chargeErr,                        // hit_sigma_integral
                                                 sumADC,                           // summedADC FIXME
                                                 nGausForFit,                      // multiplicity
                                                 numHits,                          // local_index TODO check that the order is correct
                                                 chi2PerNDF,                       // goodness_of_fit
                                                 NDF                               // dof
                                                 );
                    
                    const recob::Hit hit(hitcreator.move());
		    
                    if (!fHitFilterAlg || fHitFilterAlg->IsGoodHit(hit)) {
                        hcol.emplace_back(std::move(hit), wire, rawdigits);
                        numHits++;
                    }
                } // <---End loop over gaussians
                
                fChi2->Fill(chi2PerNDF);
	    
            }//<---End loop over merged candidate hits
           
        } //<---End looping over ROI's
        
   }//<---End looping over all the wires


    //==================================================================================================
    // End of the event
   
    // move the hit collection and the associations into the event
    hcol.put_into(evt);

} // End of produce() 


  DEFINE_ART_MODULE(GausHitFinder)

} // end of hit namespace
#endif // GAUSHITFINDER_H
