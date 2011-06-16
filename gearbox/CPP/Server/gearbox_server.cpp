#include "gearbox_server.h"

Gearbox_Server::Gearbox_Server( DDV::Socket Connection ) {
  InitializeMap( );
  srand( time( NULL ) );
  conn = Connection;
  RandomConnect = GenerateRandomString( 8 );
  RandomAuth = GenerateRandomString( 8 );
  XorPath = "";
}

Gearbox_Server::~Gearbox_Server( ) {}

void Gearbox_Server::InitializeMap( ) {
  CommandMap["OCC"] = CM_OCC;
  CommandMap["OCD"] = CM_OCD;
  CommandMap["SCA"] = CM_SCA;
  CommandMap["SCR"] = CM_SCR;
  CommandMap["SCS"] = CM_SCS;
  CommandMap["SCG"] = CM_SCG;

  CommandMap["SCS_N"] = CM_SCS_N;
  CommandMap["SCS_A"] = CM_SCS_A;
  CommandMap["SCS_S"] = CM_SCS_S;
  CommandMap["SCS_H"] = CM_SCS_H;
  CommandMap["SCS_R"] = CM_SCS_R;
  CommandMap["SCG_N"] = CM_SCG_N;
  CommandMap["SCG_A"] = CM_SCG_A;
  CommandMap["SCG_S"] = CM_SCG_S;
  CommandMap["SCG_H"] = CM_SCG_H;
  CommandMap["SCG_R"] = CM_SCG_R;
}


std::string Gearbox_Server::GenerateRandomString( int charamount ) {
  std::string Result;
  for( int i = 0; i < charamount; i++ ) {
    Result += (char)((rand() % 93)+33);
  }
  return Result;
}

std::string Gearbox_Server::GetSingleCommand( ) {
  static std::string CurCmd;
  std::string DecCmd;
  std::string Result = "";
  if( conn.ready( ) ) {
    conn.read( CurCmd );
    if( XorPath != "" ) {
      DecCmd = CurCmd;
      CurCmd = Decode( DecCmd );
    }
    if( CurCmd.find('\n') != std::string::npos ) {
      Result = CurCmd.substr(0, CurCmd.find('\n') );
      while( CurCmd[0] != '\n' ) {
        CurCmd.erase( CurCmd.begin( ) );
        if( DecCmd != "" ) {
          DecCmd.erase( DecCmd.begin( ) );
        }
      }
      CurCmd.erase( CurCmd.begin( ) );
      if( DecCmd != "" ) {
        DecCmd.erase( DecCmd.begin( ) );
      }
    }
    if( XorPath != "" ) {
      CurCmd = DecCmd;
    }
  }
  return Result;
}

std::string Gearbox_Server::Encode( std::string input ) {
  static int counter = 0;
  std::string Result;
  for( unsigned int i = 0; i < input.size( ); i ++) {
    Result.push_back( (char)( input[i] ^ XorPath[counter] ) );
    counter = (counter + 1) % XorPath.size( );
  }
  return Result;
}

std::string Gearbox_Server::Decode( std::string input ) {
  static int counter = 0;
  std::string Result;
  for( unsigned int i = 0; i < input.size( ); i ++) {
    Result.push_back( (char)( input[i] ^ XorPath[counter] ) );
    counter = (counter + 1) % XorPath.size( );
  }
  return Result;
}

void Gearbox_Server::WriteReturn( ) {
  if( XorPath == "" ) {
    conn.write( RetVal + "\n" );
  } else {
    conn.write(  Encode( RetVal + "\n" ) );
  }
}

void Gearbox_Server::Handshake( ) {
  std::deque<std::string> ConnectionParams;
  RetVal = "WELCOME" + RandomConnect;
  WriteReturn( );
  std::string Cmd;
  while( Cmd == "" ) { Cmd = GetSingleCommand( ); }
  if( Cmd.substr(0,3) != "OCC" ) {
    RetVal = "ERR";
    WriteReturn( );
    exit( 1 );
  }
  ConnectionParams = GetParameters( Cmd.substr(3) );
  if( ConnectionParams.size( ) != 2 ) {
    RetVal = "ERR_ParamAmount";
    WriteReturn( );
    exit( 1 );
  }
  if( ConnectionParams[0] != TESTUSER_ID ) {
    RetVal = "ERR_Credentials";
    WriteReturn( );
    exit( 1 );
  }
  if( ConnectionParams[1] == md5( RandomConnect + TESTUSER_STRING + RandomConnect ) ) {
    IsSrv = true;
    RetVal = "OCC" + RandomAuth;
    WriteReturn( );
    XorPath = md5( RandomAuth + TESTUSER_STRING + RandomAuth );
  } else if ( ConnectionParams[1] ==  md5( RandomConnect + TESTUSER_PASS + RandomConnect ) ) {
    IsSrv = false;
    RetVal = "OCC" + RandomAuth;
    WriteReturn( );
    XorPath = md5( RandomAuth + TESTUSER_PASS + RandomAuth );
  } else {
    RetVal = "ERR_Credentials";
    WriteReturn( );
    exit( 1 );
  }
  std::cout << ( IsSrv ? "Server Connected\n" : "Customer Connected\n" );
  std::cout << "\tCalculated xorpath: " << XorPath << "\n";
}

std::deque<std::string> Gearbox_Server::GetParameters( std::string Cmd ) {
  for( std::string::iterator it = Cmd.end( ) - 1; it >= Cmd.begin( ); it -- ) { if( (*it) == '\r' ) { Cmd.erase( it ); } }
  std::string temp;
  std::deque<std::string> Result;
  for( std::string::iterator it = Cmd.begin( ); it != Cmd.end( ); it ++ ) {
    if( (*it) == ':' ) {
      if( temp != "" ) { Result.push_back( temp ); temp = ""; }
    } else {
      temp += (*it);
    }
  }
  if( temp != "" ) { Result.push_back( temp ); }
  return Result;
}

void Gearbox_Server::HandleConnection( ) {
  bool Selector = false;
  std::map<int,Server>::iterator ServerIT;
  std::deque<std::string> Parameters;
  std::string Cmd = GetSingleCommand( );
  std::stringstream ss;
  int temp;
  if( Cmd == "" ) { return; }
  Parameters = GetParameters( Cmd.substr(3) );
  switch( CommandMap[ Cmd.substr(0,3) ] ) {
    case CM_OCD:
      RetVal = "OCD";
      if( Parameters.size( ) != 0 ) { RetVal = "ERR_ParamAmount"; }
      break;
    case CM_SCA:
      temp = ServerConfigAdd( );
      ss << temp;
      RetVal = "SCA" + ss.str();
      if( Parameters.size( ) != 0 ) { RetVal = "ERR_ParamAmount"; }
      break;
    case CM_SCR:
      RetVal = "SCR";
      if( Parameters.size( ) != 1 ) { RetVal = "ERR_ParamAmount"; break; }
      if( !ServerConfigRemove( Parameters[0] ) ) { RetVal = "ERR_InvalidID"; }
      break;
    case CM_SCS:
    case CM_SCG:
      Selector = true;
      break;
    default:
      RetVal = "ERR_InvalidCommand:" + Cmd;
  }

  if( Selector ) {
    Parameters = GetParameters( Cmd.substr(5) );
    switch( CommandMap[ Cmd.substr(0,5) ] ) {
      case CM_SCS_N:
        RetVal = "SCS_N";
        if( Parameters.size( ) != 2 ) { RetVal = "ERR_ParamAmount"; break; }
        if( RetrieveServer( Parameters[1] ) != ServerConfigs.end( ) ) { RetVal = "ERR_InvalidName"; break; }
        if( !ServerConfigSetName( Parameters[0], Parameters[1] ) ) { RetVal = "ERR_InvalidID"; break; }
        break;
      case CM_SCG_N:
        RetVal = "SCG_N";
        if( Parameters.size( ) != 1 ) { RetVal = "ERR_ParamAmount"; break; }
        ServerIT = RetrieveServer( Parameters[0] );
        if( ServerIT == ServerConfigs.end( ) ) { RetVal = "ERR_InvalidID"; break; }
        RetVal += (*ServerIT).second.SrvName;
        break;
      case CM_SCS_A:
        RetVal = "SCS_A";
        if( Parameters.size( ) != 2 ) { RetVal = "ERR_ParamAmount"; break; }
        if( !ServerConfigSetAddress( Parameters[0], Parameters[1] ) ) { RetVal = "ERR_InvalidID"; break; }
        break;
      case CM_SCG_A:
        RetVal = "SCG_A";
        if( Parameters.size( ) != 1 ) { RetVal = "ERR_ParamAmount"; break; }
        ServerIT = RetrieveServer( Parameters[0] );
        if( ServerIT == ServerConfigs.end( ) ) { RetVal = "ERR_InvalidID"; break; }
        RetVal += (*ServerIT).second.SrvAddr;
        break;
      case CM_SCS_S:
        RetVal = "SCS_S";
        if( Parameters.size( ) != 2 ) { RetVal = "ERR_ParamAmount"; break; }
        if( !atoi( Parameters[1].c_str() ) ) { RetVal = "ERR_InvalidPort"; break; }
        if( !ServerConfigSetSSH( Parameters[0], atoi( Parameters[1].c_str() ) ) ) { RetVal = "ERR_InvalidID"; break; }
        break;
      case CM_SCG_S:
        RetVal = "SCG_S";
        if( Parameters.size( ) != 1 ) { RetVal = "ERR_ParamAmount"; break; }
        ServerIT = RetrieveServer( Parameters[0] );
        if( ServerIT == ServerConfigs.end( ) ) { RetVal = "ERR_InvalidID"; break; }
        ss << (*ServerIT).second.SrvSSH;
        RetVal += ss.str();
        break;
      case CM_SCS_H:
        RetVal = "SCS_H";
        if( Parameters.size( ) != 2 ) { RetVal = "ERR_ParamAmount"; break; }
        if( !atoi( Parameters[1].c_str() ) ) { RetVal = "ERR_InvalidPort"; break; }
        if( !ServerConfigSetHTTP( Parameters[0], atoi( Parameters[1].c_str() ) ) ) { RetVal = "ERR_InvalidID"; break; }
        break;
      case CM_SCG_H:
        RetVal = "SCG_H";
        if( Parameters.size( ) != 1 ) { RetVal = "ERR_ParamAmount"; break; }
        ServerIT = RetrieveServer( Parameters[0] );
        if( ServerIT == ServerConfigs.end( ) ) { RetVal = "ERR_InvalidID"; break; }
        ss << (*ServerIT).second.SrvHTTP;
        RetVal += ss.str();
        break;
      case CM_SCS_R:
        RetVal = "SCS_R";
        if( Parameters.size( ) != 2 ) { RetVal = "ERR_ParamAmount"; break; }
        if( !atoi( Parameters[1].c_str() ) ) { RetVal = "ERR_InvalidPort"; break; }
        if( !ServerConfigSetRTMP( Parameters[0], atoi( Parameters[1].c_str() ) ) ) { RetVal = "ERR_InvalidID"; break; }
        break;
      case CM_SCG_R:
        RetVal = "SCG_R";
        if( Parameters.size( ) != 1 ) { RetVal = "ERR_ParamAmount"; break; }
        ServerIT = RetrieveServer( Parameters[0] );
        if( ServerIT == ServerConfigs.end( ) ) { RetVal = "ERR_InvalidID"; break; }
        ss << (*ServerIT).second.SrvRTMP;
        RetVal += ss.str();
        break;
      default:
        RetVal = "ERR_InvalidCommand:" + Cmd;
    }
  }

  WriteReturn( );
  if( RetVal == "OCD" ) { conn.close( ); }
}

int Gearbox_Server::ServerConfigAdd( ) {
  std::map<int,Server>::iterator it;
  if( ! ServerConfigs.empty( ) ) {
    it = ServerConfigs.end( );
    it --;
  }
  int lastid = ( ServerConfigs.empty() ? 0 : (*it).first ) + 1;
  ServerConfigs.insert( std::pair<int,Server>(lastid,(Server){lastid,"","",22,80,1935}) );
  ServerNames.insert( std::pair<int,std::string>(lastid,"") );
  return lastid;
}

bool Gearbox_Server::ServerConfigRemove( std::string Index ) {
  std::map<int,Server>::iterator it = RetrieveServer( Index );
  if( it == ServerConfigs.end( ) ) {
    return false;
  }
  ServerConfigs.erase( it );
  return true;
}

bool Gearbox_Server::ServerConfigSetName( std::string SrvID, std::string SrvName ) {
  std::map<int,Server>::iterator it = RetrieveServer( SrvID );
  if( it == ServerConfigs.end( ) ) {
    return false;
  }
  (*it).second.SrvName = SrvName;
  return true;
}

bool Gearbox_Server::ServerConfigSetAddress( std::string SrvID, std::string SrvAddress ) {
  std::map<int,Server>::iterator it = RetrieveServer( SrvID );
  if( it == ServerConfigs.end( ) ) {
    return false;
  }
  (*it).second.SrvAddr = SrvAddress;
  return true;
}

bool Gearbox_Server::ServerConfigSetSSH( std::string SrvID, int SrvSSH ) {
  std::map<int,Server>::iterator it = RetrieveServer( SrvID );
  if( it == ServerConfigs.end( ) ) {
    return false;
  }
  (*it).second.SrvSSH = SrvSSH;
  return true;
}

bool Gearbox_Server::ServerConfigSetHTTP( std::string SrvID, int SrvHTTP ) {
  std::map<int,Server>::iterator it = RetrieveServer( SrvID );
  if( it == ServerConfigs.end( ) ) {
    return false;
  }
  (*it).second.SrvHTTP = SrvHTTP;
  return true;
}

bool Gearbox_Server::ServerConfigSetRTMP( std::string SrvID, int SrvRTMP ) {
  std::map<int,Server>::iterator it = RetrieveServer( SrvID );
  if( it == ServerConfigs.end( ) ) {
    return false;
  }
  (*it).second.SrvRTMP = SrvRTMP;
  return true;
}

std::map<int,Server>::iterator Gearbox_Server::RetrieveServer( std::string Index ) {
  int Ind;
  if( atoi( Index.c_str( ) ) ) {
    Ind = atoi( Index.c_str( ) );
  } else {
    Ind = -1;
    for( std::map<int,std::string>::iterator it = ServerNames.begin(); it != ServerNames.end( ); it++ ) {
      if( (*it).second == Index ) {
        Ind = (*it).first;
      }
    }
    if( Ind == -1 ) { return ServerConfigs.end(); }
  }
  return ServerConfigs.find( Ind );
}
