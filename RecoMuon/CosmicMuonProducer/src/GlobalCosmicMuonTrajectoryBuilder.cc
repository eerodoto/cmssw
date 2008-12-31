/**
 *  Class: GlobalCosmicMuonTrajectoryBuilder
 *
 *  $Date: 2008/12/15 16:36:49 $
 *  $Revision: 1.15 $
 *  \author Chang Liu  -  Purdue University <Chang.Liu@cern.ch>
 *
 **/

#include "RecoMuon/CosmicMuonProducer/interface/GlobalCosmicMuonTrajectoryBuilder.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include "TrackingTools/PatternTools/interface/TrajectoryMeasurement.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateOnSurface.h"

#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/TrackReco/interface/TrackExtraFwd.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateTransform.h"
#include "TrackingTools/Records/interface/TransientRecHitRecord.h"
#include "TrackingTools/TransientTrackingRecHit/interface/TransientTrackingRecHitBuilder.h"

using namespace std;
using namespace edm;

//
// constructor
//
GlobalCosmicMuonTrajectoryBuilder::GlobalCosmicMuonTrajectoryBuilder(const edm::ParameterSet& par,
						                     const MuonServiceProxy* service) : theService(service) {
  ParameterSet smootherPSet = par.getParameter<ParameterSet>("SmootherParameters");
  theSmoother = new CosmicMuonSmoother(smootherPSet,theService);

  ParameterSet trackMatcherPSet = par.getParameter<ParameterSet>("GlobalMuonTrackMatcher");
  theTrackMatcher = new GlobalMuonTrackMatcher(trackMatcherPSet,theService);

  theTkTrackLabel = par.getParameter<string>("TkTrackCollectionLabel");
  theTrackerRecHitBuilderName = par.getParameter<string>("TrackerRecHitBuilder");
  theMuonRecHitBuilderName = par.getParameter<string>("MuonRecHitBuilder");
  thePropagatorName = par.getParameter<string>("Propagator");
  category_ = "Muon|RecoMuon|CosmicMuon|GlobalCosmicMuonTrajectoryBuilder";

}

//
// destructor
//

GlobalCosmicMuonTrajectoryBuilder::~GlobalCosmicMuonTrajectoryBuilder() {

  if (theSmoother) delete theSmoother;

}

//
// set Event
//
void GlobalCosmicMuonTrajectoryBuilder::setEvent(const edm::Event& event) {
  event.getByLabel(theTkTrackLabel,theTrackerTracks);

//  edm::Handle<std::vector<Trajectory> > handleTrackerTrajs;
//  if ( event.getByLabel(theTkTrackLabel,handleTrackerTrajs) && handleTrackerTrajs.isValid() ) {
//      tkTrajsAvailable = true;
//      allTrackerTrajs = &*handleTrackerTrajs;   
//      LogInfo("GlobalCosmicMuonTrajectoryBuilder") 
//	<< "Tk Trajectories Found! " << endl;
//  } else {
//      LogInfo("GlobalCosmicMuonTrajectoryBuilder") 
//	<< "No Tk Trajectories Found! " << endl;
//      tkTrajsAvailable = false;
//  }

   theService->eventSetup().get<TransientRecHitRecord>().get(theTrackerRecHitBuilderName,theTrackerRecHitBuilder);
    theService->eventSetup().get<TransientRecHitRecord>().get(theMuonRecHitBuilderName,theMuonRecHitBuilder);

}

//
// reconstruct trajectories
//
MuonCandidate::CandidateContainer GlobalCosmicMuonTrajectoryBuilder::trajectories(const TrackCand& muCand) {

  MuonCandidate::CandidateContainer result;

  if (!theTrackerTracks.isValid()) {
    LogTrace(category_)<< "Tracker Track collection is invalid!!!";
    return result;
  }

  LogTrace(category_) <<"Found "<<theTrackerTracks->size()<<" tracker Tracks";
  if (theTrackerTracks->empty()) return result;

  LogTrace(category_) <<"It has "<<theTrackerTracks->front().found()<<" tk rhs";

  reco::TrackRef muTrack = muCand.second;

  //build tracker TrackCands and pick the best match if size greater than 2
  vector<TrackCand> tkTrackCands;
  for(reco::TrackCollection::size_type i=0; i<theTrackerTracks->size(); ++i){
    reco::TrackRef tkTrack(theTrackerTracks,i);
    TrackCand tkCand = TrackCand(0,tkTrack);
    tkTrackCands.push_back(tkCand);
    LogTrace(category_) << "chisq is " << theTrackMatcher->match(muCand,tkCand,0,0);
    LogTrace(category_) << "d is " << theTrackMatcher->match(muCand,tkCand,1,0);
    LogTrace(category_) << "r_pos is " << theTrackMatcher->match(muCand,tkCand,2,0);
  }

  // match muCand to tkTrackCands
  vector<TrackCand> matched_trackerTracks = theTrackMatcher->match(muCand,tkTrackCands);

  LogTrace(category_) <<"TrackMatcher found " << matched_trackerTracks.size() << "tracker tracks matched";
  
  if ( matched_trackerTracks.empty()) return result;
  reco::TrackRef tkTrack;
  
  if(  matched_trackerTracks.size() == 1 ) {
    tkTrack = matched_trackerTracks.front().second;
  } else {
    // in case of more than 1 tkTrack,
    // select the best-one based on distance (matchOption==1)
    // at innermost Mu hit surface. (surfaceOption == 0)
    double quality = 1e6;
    double max_quality = 1e6;
    for( vector<TrackCand>::const_iterator iter = matched_trackerTracks.begin(); iter != matched_trackerTracks.end(); iter++) {
      quality = theTrackMatcher->match(muCand,*iter, 1, 0);
      LogTrace(category_) <<" quality of tracker track is " << quality;
      if( quality < max_quality ) {
        max_quality=quality;
        tkTrack = iter->second;
      }
    }
      LogTrace(category_) <<" Picked tracker track with quality " << max_quality;
  }  
  if ( tkTrack.isNull() ) return result;

  ConstRecHitContainer muRecHits;

  if (muCand.first == 0 || !muCand.first->isValid()) { 
     muRecHits = getTransientRecHits(*muTrack);
  } else {
     muRecHits = muCand.first->recHits();
  }

  LogTrace(category_)<<"mu RecHits: "<<muRecHits.size();

  ConstRecHitContainer tkRecHits = getTransientRecHits(*tkTrack);

//  if ( !tkTrajsAvailable ) {
//     tkRecHits = getTransientRecHits(*tkTrack);
//  } else {
//     tkRecHits = allTrackerTrajs->front().recHits();
//  }

  ConstRecHitContainer hits; //= tkRecHits;
  LogTrace(category_)<<"tk RecHits: "<<tkRecHits.size();

//  hits.insert(hits.end(), muRecHits.begin(), muRecHits.end());
//  stable_sort(hits.begin(), hits.end(), DecreasingGlobalY());

  sortHits(hits, muRecHits, tkRecHits);

  LogTrace(category_)<< "Used RecHits after sort: "<<hits.size()<<endl;;
  LogTrace(category_) <<utilities()->print(hits)<<endl;
  LogTrace(category_) << "== End of Used RecHits == "<<endl;

  TrajectoryStateTransform tsTrans;

  TrajectoryStateOnSurface muonState1 = tsTrans.innerStateOnSurface(*muTrack, *theService->trackingGeometry(), &*theService->magneticField());
  TrajectoryStateOnSurface tkState1 = tsTrans.innerStateOnSurface(*tkTrack, *theService->trackingGeometry(), &*theService->magneticField());

  TrajectoryStateOnSurface muonState2 = tsTrans.outerStateOnSurface(*muTrack, *theService->trackingGeometry(), &*theService->magneticField());
  TrajectoryStateOnSurface tkState2 = tsTrans.outerStateOnSurface(*tkTrack, *theService->trackingGeometry(), &*theService->magneticField());

  TrajectoryStateOnSurface firstState1 =
   ( muonState1.globalPosition().y() > tkState1.globalPosition().y() )? muonState1 : tkState1;
  TrajectoryStateOnSurface firstState2 =
   ( muonState2.globalPosition().y() > tkState2.globalPosition().y() )? muonState2 : tkState2;

  TrajectoryStateOnSurface firstState =
   ( firstState1.globalPosition().y() > firstState2.globalPosition().y() )? firstState1 : firstState2;

  if (!firstState.isValid()) return result;
  
  LogTrace(category_) <<"firstTSOS pos: "<<firstState.globalPosition()<<"mom: "<<firstState.globalMomentum();

  // begin refitting

  TrajectorySeed seed;
  vector<Trajectory> refitted = theSmoother->trajectories(seed,hits,firstState);

  if ( refitted.empty() ) refitted = theSmoother->fit(seed,hits,firstState); //FIXME

  if (refitted.empty()) {
     LogTrace(category_)<<"refit fail";
     return result;
  }

  Trajectory* myTraj = new Trajectory(refitted.front());

  const std::vector<TrajectoryMeasurement>& mytms = myTraj->measurements(); 
  LogTrace(category_)<<"measurements in final trajectory "<<mytms.size();
  LogTrace(category_) <<"Orignally there are "<<tkTrack->found()<<" tk rhs and "<<muTrack->found()<<" mu rhs.";

  if ( mytms.size() <= tkTrack->found() ) {
     LogTrace(category_)<<"insufficient measurements. skip... ";
     return result;
  }

  MuonCandidate* myCand = new MuonCandidate(myTraj,muTrack,tkTrack);
  result.push_back(myCand);
  LogTrace(category_)<<"final global cosmic muon: ";
  for (std::vector<TrajectoryMeasurement>::const_iterator itm = mytms.begin();
       itm != mytms.end(); ++itm ) {
       LogTrace(category_)<<"updated pos "<<itm->updatedState().globalPosition()
                       <<"mom "<<itm->updatedState().globalMomentum();
   }
  return result;
}

void GlobalCosmicMuonTrajectoryBuilder::sortHits(ConstRecHitContainer& hits, ConstRecHitContainer& muonHits, ConstRecHitContainer& tkHits) {

   if ( tkHits.empty() ) {
      LogTrace(category_) << "No valid tracker hits";
      return;
   }
   if ( muonHits.empty() ) {
      LogTrace(category_) << "No valid muon hits";
      return;
   }

   ConstRecHitContainer::const_iterator frontTkHit = tkHits.begin();
   ConstRecHitContainer::const_iterator backTkHit  = tkHits.end() - 1;
   while ( !(*frontTkHit)->isValid() && frontTkHit != backTkHit) {frontTkHit++;}
   while ( !(*backTkHit)->isValid() && backTkHit != frontTkHit)  {backTkHit--;}

   ConstRecHitContainer::const_iterator frontMuHit = muonHits.begin();
   ConstRecHitContainer::const_iterator backMuHit  = muonHits.end() - 1;
   while ( !(*frontMuHit)->isValid() && frontMuHit != backMuHit) {frontMuHit++;}
   while ( !(*backMuHit)->isValid() && backMuHit != frontMuHit)  {backMuHit--;}

   if ( frontTkHit == backTkHit ) {
      LogTrace(category_) << "No valid tracker hits";
      return;
   }
   if ( frontMuHit == backMuHit ) {
      LogTrace(category_) << "No valid muon hits";
      return;
   }

  GlobalPoint frontTkPos = (*frontTkHit)->globalPosition();
  GlobalPoint backTkPos = (*backTkHit)->globalPosition();

  GlobalPoint frontMuPos = (*frontMuHit)->globalPosition();
  GlobalPoint backMuPos = (*backMuHit)->globalPosition();

  //sort hits going from higher to lower positions
  if ( frontTkPos.y() < backTkPos.y() )  {//check if tk hits order same direction
    reverse(tkHits.begin(), tkHits.end());
  }

  if ( frontMuPos.y() < backMuPos.y() )  {
    reverse(muonHits.begin(), muonHits.end());
  }

  LogTrace(category_)<< "tkHits after sort: "<<tkHits.size()<<endl;;
  LogTrace(category_) <<utilities()->print(tkHits)<<endl;
  LogTrace(category_) << "== End of tkHits == "<<endl;

  LogTrace(category_)<< "muonHits after sort: "<<muonHits.size()<<endl;;
  LogTrace(category_) <<utilities()->print(muonHits)<<endl;
  LogTrace(category_)<< "== End of muonHits == "<<endl;

  //separate muon hits into 2 different hemisphere
  ConstRecHitContainer::iterator middlepoint = muonHits.begin();
  bool insertInMiddle = false;

  for (ConstRecHitContainer::iterator ihit = muonHits.begin(); 
       ihit != muonHits.end() - 1; ihit++ ) {
    GlobalPoint ipos = (*ihit)->globalPosition();
    GlobalPoint nextpos = (*(ihit+1))->globalPosition();
    GlobalPoint middle((ipos.x()+nextpos.x())/2, (ipos.y()+nextpos.y())/2, (ipos.z()+nextpos.z())/2);
    LogTrace(category_)<<"ipos "<<ipos<<"nextpos"<<nextpos<<" middle "<<middle<<endl;
    if ( (middle.perp() < ipos.perp()) && (middle.perp() < nextpos.perp() ) ) {
      LogTrace(category_)<<"found middlepoint"<<endl;
      middlepoint = ihit;
      insertInMiddle = true;
      break;
    }
  }

  //insert track hits in correct order
  if ( insertInMiddle ) { //if tk hits should be sandwich
    GlobalPoint jointpointpos = (*middlepoint)->globalPosition();
    LogTrace(category_)<<"jointpoint "<<jointpointpos<<endl;
    if ((frontTkPos - jointpointpos).mag() > (backTkPos - jointpointpos).mag() ) {//check if tk hits order same direction
      reverse(tkHits.begin(), tkHits.end());
    }
    muonHits.insert(middlepoint+1, tkHits.begin(), tkHits.end());
    hits = muonHits; 
  } else { // append at one end
    if ( frontTkPos.y() < frontMuPos.y() ) { //insert at the end
      LogTrace(category_)<<"insert at the end "<<frontTkPos << frontMuPos <<endl;

      hits = muonHits; 
      hits.insert(hits.end(), tkHits.begin(), tkHits.end());
    } else { //insert at the beginning
      LogTrace(category_)<<"insert at the beginning "<<frontTkPos << frontMuPos <<endl;
      hits = tkHits;
      hits.insert(hits.end(), muonHits.begin(), muonHits.end());
    }
  }
}


TransientTrackingRecHit::ConstRecHitContainer
GlobalCosmicMuonTrajectoryBuilder::getTransientRecHits(const reco::Track& track) const {

  TransientTrackingRecHit::ConstRecHitContainer result;

  TrajectoryStateTransform tsTrans;

  TrajectoryStateOnSurface currTsos = tsTrans.innerStateOnSurface(track, *theService->trackingGeometry(), &*theService->magneticField());
  for (trackingRecHit_iterator hit = track.recHitsBegin(); hit != track.recHitsEnd(); ++hit) {
    if((*hit)->isValid()) {
      DetId recoid = (*hit)->geographicalId();
      if ( recoid.det() == DetId::Tracker ) {
        TransientTrackingRecHit::RecHitPointer ttrhit = theTrackerRecHitBuilder->build(&**hit);
        TrajectoryStateOnSurface predTsos =  theService->propagator(thePropagatorName)->propagate(currTsos, theService->trackingGeometry()->idToDet(recoid)->surface());
        LogTrace(category_)<<"predtsos "<<predTsos.isValid();
        if ( predTsos.isValid() ) {
          currTsos = predTsos;
          TransientTrackingRecHit::RecHitPointer preciseHit = ttrhit->clone(currTsos);
          result.push_back(preciseHit);
       }
      } else if ( recoid.det() == DetId::Muon ) {
	result.push_back(theMuonRecHitBuilder->build(&**hit));
      }
    }
  }
  return result;
}
