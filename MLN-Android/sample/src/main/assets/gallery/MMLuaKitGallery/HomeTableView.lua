LuaR  

            @ 
@@
ΐ@
@A
ΐA
@Be   
@ e@  
@e  
@ eΐ  
@e  
@ e@ 
@e 
@           _name    HomeTableView 	   _version    1.0    _type            TYPE_FOLLOW    follow    TYPE_RECOMMEND 
   recommend    new 
   tableView    setupContainerView    updateCategoryId 
   searchBox    setupDataSource    request              K    @ ΐ  A  
@ΐ@  
 
@A_          setmetatable    __index 	   dataList    Array    cid       π?         W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua    	   
   
   
   
   
                           self           o             _ENV           
@ @@ @ @ @ ΐ@           _type    setupContainerView    setupDataSource 
   tableView         W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua                                  self           type               ?    	3   A   @  Ζ@  C έΑΐA Α@   AAA ΑAAΒ A
ΐB  LΑΒΐ ]A
 LCε  ]ALACεA  ]ALCΐ %  ]A LCΐ %Β  ]A LΑCε ]ALDΐ %B ]A LDΐ % ]A         firstCellType    normalCellType 
   TableView    width    window    height    MeasurementType    MATCH_PARENT    showScrollIndicator 
   tableView    TableViewAutoFitAdapter    adapter    sectionCount 	   rowCount    initCellByReuseId    reuseId    fillCellDataByReuseId       !                        π?        W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua            !           "   $       F @ L@ΐ ^  _        	   dataList    size         W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua    #   #   #   #   $         _              self %   (    	   E   L@ΐ ] 
@ G@ Lΐΐ Η @ ]@     
   searchBar 
   searchBox    contentView    addView         W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua 	   &   &   &   &   '   '   '   '   (         cell     	         self )   2        F @ @  ] ΐΐ  
  A @A@ A @  Α ΖΐΑ ΐ @@  B D  @  Α Ζ@Β ΐ  @  B  D  @   
      require    MMLuaKitGallery.HomeCommonCell    _cell    new    contentView    addView    _type    TYPE_FOLLOW    updateFollowLabel    TYPE_RECOMMEND           W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua     *   *   *   +   +   +   ,   ,   ,   ,   ,   ,   -   -   -   -   .   .   .   .   .   .   /   /   /   /   0   0   0   0   0   2         cell         
   cellClass              _ENV    self 3   8        ΐ @                     π?       W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua    4   4   5   5   7   7   8         _           row              searchBarCellId    normalCellId 9   :                     W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua    :         _           _           _            ;   >    	   Ζ @ Μ@ΐ@ έ@ Α@A     	   dataList    get    _cell    updateCellContentWithItem         W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua 	   <   <   <   <   =   =   =   =   >         cell     	      _     	      row     	      item    	         self      W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua 3                                                                                 !      "   $   "   %   %   (   %   )   )   2   )   3   8   3   9   9   :   9   ;   ;   >   ;   ?         self     3      searchBarCellId    3      normalCellId    3   
   tableView    3      adapter    3         _ENV @   B       
@         cid         W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua    A   B         self           cid            C   F    
   F @ @  ] Lΐ ] ΐΐ               require    MMLuaKitGallery.SearchBox    new    setup          W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua 
   D   D   D   D   D   E   E   E   E   F         self     
      search    
         _ENV G   b       
@@L@ Γ  %  ]@ Gΐ@ L Α ε@  ]@Gΐ@ L@Α ε  ]@        minId       π?   request 
   tableView    setRefreshingCallback    setLoadingCallback    I   M            @ @@@      
   tableView    reloadData         W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua    J   J   K   K   K   M         success           _              self N   V            @   ε   @         request    O   U        @ @@@  @ @@      @ ΐ@@      
   tableView    stopRefreshing    resetLoading    reloadData          W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua    P   P   P   Q   Q   Q   R   R   S   S   S   U         success           _              self     W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua    O   O   O   U   O   V             self W   a            @    ε   @         request    X   `        @ @@@ [@    @ @@      @ ΐ@@      
   tableView    stopLoading    noMoreData    reloadData          W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua    Y   Y   Y   Z   Z   [   [   [   ]   ]   ^   ^   ^   `         success           data              self     W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua    X   X   X   `   X   a             self     W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua    H   I   I   M   I   N   N   V   N   W   W   a   W   b         self            c   x       Α   A@ @   ΐ Α  @Φ@  @Φ@AA Aε  A         gallery/json/fashion.json    System    Android 
   assets://    file://    File    asyncReadFile    j   w        @@ @   ΐ@ ΐ @  AA Ε  Ϋ   @  @   ΐ ΖAΜΐΑ@ έ@Ε  @ έ@ΐ  Γ     @        map    StringUtil 
   jsonToMap            get    data 	   dataList    addAll           W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua     k   k   k   k   k   l   l   m   m   m   m   n   n   n   o   o   p   p   q   q   q   q   s   s   s   s   s   u   u   u   u   w         codeNumber         	   response            data             _ENV    first    self 	   complete      W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua    d   e   e   e   e   e   f   f   f   f   h   h   h   j   j   j   w   j   x         self           first        	   complete        	   filepath             _ENV     W   @/Users/momo/workspace/MLNSDKCaseTest/Deploy/gallery/MMLuaKitGallery/HomeTableView.lua                                  ?      B   @   F   C   b   G   x   c   y   y         _class             _ENV 