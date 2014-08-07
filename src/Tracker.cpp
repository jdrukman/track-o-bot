#include "Tracker.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QTimer>
#include <QDesktopServices>

#include "Json.h"
#include "Hearthstone.h"

#define DEFAULT_WEBSERVICE_URL "https://trackobot.com"

DEFINE_SINGLETON_SCOPE( Tracker );

Tracker::Tracker() {
  successfulResultCount = 0;
  unknownOutcomeCount = 0;
  unknownModeCount = 0;
  unknownOrderCount = 0;
  unknownClassCount = 0;
  unknownOpponentCount = 0;

  connect( &networkManager,
      SIGNAL( sslErrors(QNetworkReply*, const QList<QSslError>&) ),
      this,
      SLOT( SSLErrors(QNetworkReply*, const QList<QSslError>&) ) );
}

Tracker::~Tracker() {
}

void Tracker::EnsureAccountIsSetUp() {
  if( !IsAccountSetUp() ) {
    LOG( "No account setup. Creating one for you." );
    CreateAndStoreAccount();
  } else {
    LOG( "Account %s found", Username().toStdString().c_str() );
  }
}

void Tracker::AddResult( GameMode mode, Outcome outcome, GoingOrder order, Class ownClass, Class opponentClass, const CardHistoryList& historyCardList )
{
#ifndef _DEBUG
  if( mode == MODE_PRACTICE ) {
    LOG( "Ignore practice game." ); // only in Non Debug Versions
    return;
  }
#endif

#ifdef _DEBUG
  string cardHistoryOutput;
  for( CardHistoryList::const_iterator it = historyCardList.begin(); it != historyCardList.end(); ++it ) {
    cardHistoryOutput += (*it).player == PLAYER_SELF ? "SELF " : "OPPONENT ";
    cardHistoryOutput += (*it).cardId + "\n";
  }
  LOG( "Card History: %s", cardHistoryOutput.c_str() );
#endif

  if( order == ORDER_UNKNOWN ) {
    // Order marker wasn't found, so try to look up the order in the card history
    for( CardHistoryList::const_iterator it = historyCardList.begin(); it != historyCardList.end(); ++it ) {
      if( (*it).cardId == "GAME_005" ) { // The Coin was played...
        if( (*it).player == PLAYER_SELF ) { // ...by me?
          LOG( "Order fallback. Went second" ); // Yes, so I went second
          order = ORDER_SECOND;
        } else {
          LOG( "Order fallback. Went first" ); // Nope, so the opponent went second and I went first
          order = ORDER_FIRST;
        }
        break;
      }
    }
  }

  if( outcome == OUTCOME_UNKNOWN ) {
    unknownOutcomeCount++;
    LOG( "Outcome unknown. Skip result" );
    return;
  }

  if( mode == MODE_UNKNOWN ) {
    unknownModeCount++;
    LOG( "Mode unknown. Skip result" );
    return;
  }

  if( order == ORDER_UNKNOWN ) {
    unknownOrderCount++;
    LOG( "Order unknown. Skip result" );
    return;
  }

  if( ownClass == CLASS_UNKNOWN ) {
    unknownClassCount++;
    LOG( "Own Class unknown. Skip result" );
    return;
  }

  if( opponentClass == CLASS_UNKNOWN ) {
    unknownOpponentCount++;
    LOG( "Class of Opponent unknown. Skip result" );
    return;
  }

  successfulResultCount++;

  LOG( "Upload %s %s vs. %s as %s. Went %s",
      MODE_NAMES[ mode ],
      OUTCOME_NAMES[ outcome ],
      CLASS_NAMES[ opponentClass ],
      CLASS_NAMES[ ownClass ],
      ORDER_NAMES[ order ] );

  QtJson::JsonObject result;
  result[ "coin" ]     = ( order == ORDER_SECOND );
  result[ "hero" ]     = CLASS_NAMES[ ownClass ];
  result[ "opponent" ] = CLASS_NAMES[ opponentClass ];
  result[ "win" ]      = ( outcome == OUTCOME_VICTORY );
  result[ "mode" ]     = MODE_NAMES[ mode ];

  QtJson::JsonArray card_history;
  for( CardHistoryList::const_iterator it = historyCardList.begin(); it != historyCardList.end(); ++it ) {
    QtJson::JsonObject item;
    item[ "player" ] = (*it).player == PLAYER_SELF ? "me" : "opponent";
    item[ "card_id" ] = (*it).cardId.c_str();
    card_history.append(item);
  }
  result[ "card_history" ] = card_history;

  QtJson::JsonObject params;
  params[ "result" ] = result;

  // Some metadata to find out room for improvement
  QtJson::JsonArray meta;

  meta.append( successfulResultCount );
  meta.append( unknownOutcomeCount );
  meta.append( unknownModeCount );
  meta.append( unknownOrderCount );
  meta.append( unknownClassCount );
  meta.append( unknownOpponentCount );
  meta.append( Hearthstone::Instance()->GetWidth() );
  meta.append( Hearthstone::Instance()->GetHeight() );
  meta.append( VERSION );
  meta.append( PLATFORM );

  params[ "_meta" ] = meta;

  QByteArray data = QtJson::serialize( params );

  QNetworkReply *reply = AuthPostJson( "/profile/results.json", data );
  connect( reply, SIGNAL( finished() ), this, SLOT( AddResultHandleReply() ) );
}

QNetworkReply* Tracker::AuthPostJson( const QString& path, const QByteArray& data ) {
  QString credentials = "Basic " + ( Username() + ":" + Password() ).toAscii().toBase64();

  QNetworkRequest request = CreateTrackerRequest( path );
  request.setRawHeader( "Authorization", credentials.toAscii() );
  request.setHeader( QNetworkRequest::ContentTypeHeader, "application/json" );
  return networkManager.post( request, data );
}

QNetworkRequest Tracker::CreateTrackerRequest( const QString& path ) {
  QUrl url( WebserviceURL( path ) );
  QNetworkRequest request( url );
  request.setRawHeader( "User-Agent", "Track-o-Bot/" VERSION PLATFORM );
  return request;
}

void Tracker::AddResultHandleReply() {
  QNetworkReply *reply = static_cast< QNetworkReply* >( sender() );
  if( reply->error() == QNetworkReply::NoError ) {
    LOG( "Result was uploaded successfully!" );
  } else {
    int statusCode = reply->attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt();
    LOG( "There was a problem uploading the result. Error: %i HTTP Status Code: %i", reply->error(), statusCode );
  }
}

void Tracker::CreateAndStoreAccount() {
  QNetworkRequest request = CreateTrackerRequest( "/users.json" );
  QNetworkReply *reply = networkManager.post( request, "" );
  connect( reply, SIGNAL(finished()), this, SLOT(CreateAndStoreAccountHandleReply()) );
}

void Tracker::CreateAndStoreAccountHandleReply() {
  QNetworkReply *reply = static_cast< QNetworkReply* >( sender() );
  if( reply->error() == QNetworkReply::NoError ) {
    LOG( "Account creation was successful!" );

    QByteArray jsonData = reply->readAll();

    bool ok;
    QtJson::JsonObject user = QtJson::parse( jsonData, ok ).toMap();

    if( !ok ) {
      LOG( "Couldn't parse response" );
    } else {
      LOG( "Welcome %s", user[ "username" ].toString().toStdString().c_str() );

      SetUsername( user["username"].toString() );
      SetPassword( user["password"].toString() );

      emit AccountCreated();
    }
  } else {
    int statusCode = reply->attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt();
    LOG( "There was a problem creating an account. Error: %i HTTP Status Code: %i", reply->error(), statusCode );
  }
}

void Tracker::OpenProfile() {
  QNetworkReply *reply = AuthPostJson( "/one_time_auth.json", "" );
  connect( reply, SIGNAL( finished() ), this, SLOT( OpenProfileHandleReply() ) );
}

void Tracker::OpenProfileHandleReply() {
  QNetworkReply *reply = static_cast< QNetworkReply* >( sender() );
  if( reply->error() == QNetworkReply::NoError ) {
    QByteArray jsonData = reply->readAll();

    bool ok;
    QtJson::JsonObject response = QtJson::parse( jsonData, ok ).toMap();

    if( !ok ) {
      LOG( "Couldn't parse response" );
    } else {
      QString url = response[ "url" ].toString();
      QDesktopServices::openUrl( QUrl( url ) );
    }
  } else {
    int statusCode = reply->attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt();
    LOG( "There was a problem creating an auth token. Error: %i HTTP Status Code: %i", reply->error(), statusCode );
  }
}

QString Tracker::Username() {
  return settings.value( "username" ).toString();
}

QString Tracker::Password() {
  return settings.value( "password" ).toString();
}

QString Tracker::WebserviceURL(const QString& path) {
  return WebserviceURL() + path;
}

QString Tracker::WebserviceURL() {
  if(!settings.contains("webserviceUrl") || settings.value("webserviceUrl").toString().isEmpty()) {
    SetWebserviceURL(DEFAULT_WEBSERVICE_URL);
  }

  return settings.value("webserviceUrl").toString();
}

void Tracker::SetUsername(const QString& username) {
  settings.setValue("username", username);
}

void Tracker::SetPassword(const QString& password) {
  settings.setValue("password", password);
}

void Tracker::SetWebserviceURL(const QString& webserviceUrl) {
  settings.setValue("webserviceUrl", webserviceUrl);
}

bool Tracker::IsAccountSetUp() {
  return settings.contains("username") && settings.contains("password") &&
    !settings.value("username").toString().isEmpty() && !settings.value("password").toString().isEmpty();
}

// Allow self-signed certificates because Qt might report
// "There root certificate of the certificate chain is self-signed, and untrusted"
// The root cert might not be trusted yet (only after we browse to the website)
// So allow allow self-signed certificates, just in case
void Tracker::SSLErrors(QNetworkReply *reply, const QList<QSslError>& errors) {
  QList<QSslError> errorsToIgnore;

  QList<QSslError>::const_iterator cit;
  for(cit = errors.begin(); cit != errors.end(); ++cit) {
    const QSslError& err = *cit;
    if(err.error() == QSslError::SelfSignedCertificate ||
       err.error() == QSslError::SelfSignedCertificateInChain)
    {
      errorsToIgnore << err;
    }
  }

  reply->ignoreSslErrors(errorsToIgnore);
}

