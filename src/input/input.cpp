#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <mist/defines.h>
#include "input.h"
#include <sstream>
#include <fstream>
#include <iterator>

namespace Mist {
  Input * Input::singleton = NULL;
  
  void Input::userCallback(char * data, size_t len, unsigned int id){
    long tid = ((long)(data[0]) << 24) | ((long)(data[1]) << 16) | ((long)(data[2]) << 8) | ((long)(data[3]));
    long keyNum = ((long)(data[4]) << 8) | ((long)(data[5]));
    bufferFrame(tid, keyNum + 1);//Try buffer next frame
  }
  
  void Input::doNothing(char * data, size_t len, unsigned int id){
    DEBUG_MSG(DLVL_DONTEVEN, "Doing 'nothing'");
    for (int i = 0; i < 5; i++){
      int tmp = ((long)(data[i*6]) << 24) | ((long)(data[i*6 + 1]) << 16) | ((long)(data[i*6 + 2]) << 8) | data[i*6 + 3];
      if (tmp){
        singleton->userCallback(data + (i*6), 6, id);//call the userCallback for this input
      }
    }
  }
  
  Input::Input(Util::Config * cfg) {
    config = cfg;
    JSON::Value option;
    option["long"] = "json";
    option["short"] = "j";
    option["help"] = "Output MistIn info in JSON format, then exit.";
    option["value"].append(0ll);
    config->addOption("json", option);
    option.null();
    option["arg_num"] = 1ll;
    option["arg"] = "string";
    option["help"] = "Name of the input file or - for stdin";
    option["value"].append("-");
    config->addOption("input", option);
    option.null();
    option["arg_num"] = 2ll;
    option["arg"] = "string";
    option["help"] = "Name of the output file or - for stdout";
    option["value"].append("-");
    config->addOption("output", option);
    option.null();
    option["arg"] = "string";
    option["short"] = "s";
    option["long"] = "stream";
    option["help"] = "The name of the stream that this connector will transmit.";
    config->addOption("streamname", option);
    option.null();
    option["short"] = "p";
    option["long"] = "player";
    option["help"] = "Makes this connector into a player";
    config->addOption("player", option);
    
    packTime = 0;
    lastActive = Util::epoch();
    playing = 0;
    playUntil = 0;
    
    singleton = this;
    isBuffer = false;
  }

  int Input::run() {
    if (config->getBool("json")) {
      std::cerr << capa.toString() << std::endl;
      return 0;
    }
    if (!setup()) {
      std::cerr << config->getString("cmd") << " setup failed." << std::endl;
      return 0;
    }
    if (!readHeader()) {
      std::cerr << "Reading header for " << config->getString("input") << " failed." << std::endl;
      return 0;
    }
    parseHeader();
    
    if (!config->getBool("player")){
      //check filename for no -
      if (config->getString("output") != "-"){
        //output to dtsc
        DTSC::Meta newMeta = myMeta;
        newMeta.reset();
        JSON::Value tempVal;
        std::ofstream file(config->getString("output").c_str());
        long long int bpos = 0;
        seek(0);
        getNext();
        while (lastPack){
          tempVal = lastPack.toJSON();
          tempVal["bpos"] = bpos;
          newMeta.update(tempVal);
          file << std::string(lastPack.getData(), lastPack.getDataLen());
          bpos += lastPack.getDataLen();
          getNext();
        }
        //close file
        file.close();
        //create header
        file.open((config->getString("output")+".dtsh").c_str());
        file << newMeta.toJSON().toNetPacked();
        file.close();
      }else{
        DEBUG_MSG(DLVL_FAIL,"No filename specified, exiting");
      }
    }else{
      //after this player functionality
      
      metaPage.init(config->getString("streamname"), (isBuffer ? 8388608 : myMeta.getSendLen()), true);
      myMeta.writeTo(metaPage.mapped);
      userPage.init(config->getString("streamname") + "_users", 30, true);
      
      
      if (!isBuffer){
        for (std::map<int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
          bufferFrame(it->first, 0);
        }
      }
      
      sem_t * waiting = sem_open(std::string("/wait_" + config->getString("streamname")).c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 0);
      if (waiting == SEM_FAILED){
        DEBUG_MSG(DLVL_FAIL, "Failed to open semaphore - cancelling");
        return -1;
      }
      sem_post(waiting);
      sem_close(waiting);
      
      DEBUG_MSG(DLVL_HIGH,"Pre-While");
      
      long long int activityCounter = Util::getMS();
      while ((Util::getMS() - activityCounter) < 10000){//1minute timeout
        DEBUG_MSG(DLVL_HIGH, "Timer running");
        Util::sleep(1000);
        removeUnused();
        userPage.parseEach(doNothing);
        if (userPage.amount){
          activityCounter = Util::getMS();
          DEBUG_MSG(DLVL_HIGH, "Connected users: %d", userPage.amount);
        }
      }
      DEBUG_MSG(DLVL_DEVEL,"Closing clean");
      //end player functionality
    }
    return 0;
  }

  void Input::removeUnused(){
    for (std::map<unsigned int, std::map<unsigned int, unsigned int> >::iterator it = pageCounter.begin(); it != pageCounter.end(); it++){
      for (std::map<unsigned int, unsigned int>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++){
        it2->second--;
      }
      bool change = true;
      while (change){
        change = false;
        for (std::map<unsigned int, unsigned int>::iterator it2 = it->second.begin(); it2 != it->second.end(); it2++){
          if (!it2->second){
            DEBUG_MSG(DLVL_DEVEL, "Erasing page %u:%u", it->first, it2->first);
            pagesByTrack[it->first].erase(it2->first); 
            pageCounter[it->first].erase(it2->first);
            change = true;
            break;
          }
        }
      }
    }
  }
  
  void Input::parseHeader(){
    DEBUG_MSG(DLVL_DEVEL,"Parsing the header");
    //Select all tracks for parsing header
    selectedTracks.clear();
    std::stringstream trackSpec;
    for (std::map<int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
      DEBUG_MSG(DLVL_VERYHIGH, "Track %d encountered", it->first);
      //selectedTracks.insert(it->first);
      if (trackSpec.str() != ""){
        trackSpec << " ";
      }
      trackSpec << it->first;
      DEBUG_MSG(DLVL_VERYHIGH, "Trackspec now %s", trackSpec.str().c_str());
      for (std::deque<DTSC::Key>::iterator it2 = it->second.keys.begin(); it2 != it->second.keys.end(); it2++){
        keyTimes[it->first].insert(it2->getTime());
      }
    }
    trackSelect(trackSpec.str());
    
    std::map<int, DTSCPageData> curData;
    std::map<int, booking> bookKeeping;
    
    seek(0);
    getNext();

    while(lastPack){//loop through all
      int tid = lastPack.getTrackId();
      if (!bookKeeping.count(tid)){
        bookKeeping[tid].first = 0;
        bookKeeping[tid].curPart = 0;
        bookKeeping[tid].curKey = 0;
        
        curData[tid].lastKeyTime = 0xFFFFFFFF;
        curData[tid].keyNum = 1;
        curData[tid].partNum = 0;
        curData[tid].dataSize = 0;
        curData[tid].curOffset = 0;
        curData[tid].firstTime = myMeta.tracks[tid].keys[0].getTime();

        char tmpId[20];
        sprintf(tmpId, "%d", tid);
        indexPages[tid].init(config->getString("streamname") + tmpId, 8192, true);//Pages of 8kb in size, room for 512 parts.
      }
      if (myMeta.tracks[tid].keys[bookKeeping[tid].curKey].getParts() == curData[tid].partNum){
        if (curData[tid].dataSize > 8388608) {
          pagesByTrack[tid][bookKeeping[tid].first] = curData[tid];
          bookKeeping[tid].first += curData[tid].keyNum;
          curData[tid].keyNum = 0;
          curData[tid].dataSize = 0;
          curData[tid].firstTime = myMeta.tracks[tid].keys[bookKeeping[tid].curKey].getTime();
        }
        bookKeeping[tid].curKey++;
        curData[tid].keyNum++;
        curData[tid].partNum = 0;
      }
      curData[tid].dataSize += lastPack.getDataLen();
      curData[tid].partNum ++;
      bookKeeping[tid].curPart ++;
      getNext(false);
    }
    for (std::map<int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
      if (curData.count(it->first) && !pagesByTrack[it->first].count(bookKeeping[it->first].first)){
        pagesByTrack[it->first][bookKeeping[it->first].first] = curData[it->first];
      }
      if (!pagesByTrack.count(it->first)){
        DEBUG_MSG(DLVL_WARN, "No pages for track %d found", it->first);
      }else{
        DEBUG_MSG(DLVL_HIGH, "Track %d (%s) split into %lu pages", it->first, myMeta.tracks[it->first].codec.c_str(), pagesByTrack[it->first].size());
        for (std::map<int, DTSCPageData>::iterator it2 = pagesByTrack[it->first].begin(); it2 != pagesByTrack[it->first].end(); it2++){
        }
      }
    }
  }
  
  
  bool Input::bufferFrame(int track, int keyNum){
    DEBUG_MSG(DLVL_DONTEVEN, "Attempting to buffer %d:%d", track, keyNum);
    if (!pagesByTrack.count(track)){
      return false;
    }
    std::map<int, DTSCPageData> ::iterator it = pagesByTrack[track].upper_bound(keyNum);
    if (it == pagesByTrack[track].begin()){
      return false;
    }
    it --;
    int pageNum = it->first;
    pageCounter[track][pageNum] = 15;///Keep page 15seconds in memory after last use
    
    if (!dataPages[track].count(pageNum)){
      char pageId[100];
      int pageIdLen = sprintf(pageId, "%s%d_%d", config->getString("streamname").c_str(), track, pageNum);
      std::string tmpString(pageId, pageIdLen);
      dataPages[track][pageNum].init(tmpString, it->second.dataSize, true);
      DEBUG_MSG(DLVL_HIGH, "Buffering page %d through %d / %lu", pageNum, pageNum + it->second.keyNum, myMeta.tracks[track].keys.size());
        
      std::stringstream trackSpec;
      trackSpec << track;
      trackSelect(trackSpec.str());
    }else{
      return true;
    }
    seek(myMeta.tracks[track].keys[pageNum].getTime());
    long long unsigned int stopTime = myMeta.tracks[track].lastms + 1;
    if ((int)myMeta.tracks[track].keys.size() > pageNum + it->second.keyNum){
      stopTime = myMeta.tracks[track].keys[pageNum + it->second.keyNum].getTime();
    }
    DEBUG_MSG(DLVL_HIGH, "Playing from %ld to %llu", myMeta.tracks[track].keys[pageNum].getTime(), stopTime);
    getNext();
    while (lastPack && lastPack.getTime() < stopTime){
      if (it->second.curOffset + lastPack.getDataLen() > pagesByTrack[track][pageNum].dataSize){
        DEBUG_MSG(DLVL_WARN, "Trying to write %u bytes past the end of page %u/%u", lastPack.getDataLen(), track, pageNum);
        return true;
      }else{
        memcpy(dataPages[track][pageNum].mapped + it->second.curOffset, lastPack.getData(), lastPack.getDataLen());
        it->second.curOffset += lastPack.getDataLen();
      }
      getNext();
    }
    for (int i = 0; i < indexPages[track].len / 8; i++){
      if (((long long int*)indexPages[track].mapped)[i] == 0){
        ((long long int*)indexPages[track].mapped)[i] = (((long long int)htonl(pageNum)) << 32) | htonl(it->second.keyNum);
        break;
      }
    }
    return true;
  }
  
  bool Input::atKeyFrame(){
    static std::map<int, int> lastSeen;
    //not in keyTimes? We're not at a keyframe.
    unsigned int c = keyTimes[lastPack.getTrackId()].count(lastPack.getTime());
    if (!c){
      return false;
    }
    //skip double times
    if (lastSeen.count(lastPack.getTrackId()) && lastSeen[lastPack.getTrackId()] == lastPack.getTime()){
      return false;
    }
    //set last seen, and return true
    lastSeen[lastPack.getTrackId()] = lastPack.getTime();
    return true;
  }
  
  void Input::play(int until) {
    playing = -1;
    playUntil = until;
    initialTime = 0;
    benchMark = Util::getMS();
  }

  void Input::playOnce() {
    if (playing <= 0) {
      playing = 1;
    }
    ++playing;
    benchMark = Util::getMS();
  }

  void Input::quitPlay() {
    playing = 0;
  }
}

