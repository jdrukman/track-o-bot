#include "ResultQueue.h"

#define RESULT_QUEUE_CHECK_PERIOD (30 * 60 * 1000)
#define RESULT_QUEUE_UPLOAD_PERIOD (5 * 60 * 1000)

ResultQueue::ResultQueue()
{
  connect( &mWebProfile, SIGNAL( UploadResultFailed(const QJsonObject&, int) ),
      this, SLOT( UploadResultFailed(const QJsonObject&, int) ) );
  connect( &mWebProfile, SIGNAL( UploadResultSucceeded(const QJsonObject&) ),
      this, SLOT( UploadResultSucceeded(const QJsonObject&) ) );

  mCheckTimer = new QTimer( this );
  connect( mCheckTimer, SIGNAL( timeout() ), this, SLOT( Check() ) );
  mCheckTimer->start( RESULT_QUEUE_CHECK_PERIOD );

  mUploadTimer = new QTimer( this );
  connect( mUploadTimer, SIGNAL( timeout() ), this, SLOT( Upload() ) );

  Load();
}

ResultQueue::~ResultQueue() {
  Save();
}

void ResultQueue::Load() {
  QSettings settings;
  if( settings.contains( "resultsQueue" ) ) {
    QJsonDocument doc = QJsonDocument::fromJson(
        settings.value( "resultsQueue" ).toByteArray() );
    mQueue = doc.array();
    LOG( "%d unsaved results found", mQueue.size() );

    settings.remove( "resultsQueue" );
  }
}

void ResultQueue::Save() {
  LOG( "Saving %d results", mQueue.size() );
  QSettings().setValue( "resultsQueue", QJsonDocument( mQueue ).toJson() );
}

void ResultQueue::Add( const Result& res ) {
  if( res.mode == MODE_SOLO_ADVENTURES ) {
    LOG( "Ignore solo adventure" );
    return;
  }

  if( res.mode == MODE_TAVERN_BRAWL ) {
    LOG( "Ignore tavern brawl" );
    return;
  }

  if( res.outcome == OUTCOME_UNKNOWN ) {
    LOG( "Outcome unknown. Skip result" );
    return;
  }

  if( res.mode == MODE_UNKNOWN ) {
    LOG( "Mode unknown. Skip result" );
    return;
  }

  if( res.order == ORDER_UNKNOWN ) {
    LOG( "Order unknown. Skip result" );
    return;
  }

  if( res.hero == CLASS_UNKNOWN ) {
    LOG( "Own Class unknown. Skip result" );
    return;
  }

  if( res.opponent == CLASS_UNKNOWN ) {
    LOG( "Class of Opponent unknown. Skip result" );
    return;
  }

  LOG( "Result: %s %s vs. %s as %s. Went %s",
      MODE_NAMES[ res.mode ],
      OUTCOME_NAMES[ res.outcome ],
      CLASS_NAMES[ res.opponent ],
      CLASS_NAMES[ res.hero ],
      ORDER_NAMES[ res.order ] );

  mQueue.append( res.AsJson() );
  Upload();
}

void ResultQueue::UploadResultFailed( const QJsonObject& result, int errorCode ) {
  ERR( "There was a problem uploading the result (Code %d). Will save the result locally and try again later.", errorCode );
  mQueue.append( result );

  // Upload not working, check periodically from now on
  mUploadTimer->stop();
}

void ResultQueue::UploadResultSucceeded( const QJsonObject& response ) {
  int id = response[ "result" ].toObject()[ "id" ].toInt();
  if( id ) {
    emit ResultUploaded( id );
  } else {
    ERR( "Response without id received" );
  }

  // If we have items in the queue, it's time to slowly roll them out
  mUploadTimer->start( RESULT_QUEUE_UPLOAD_PERIOD );
}

void ResultQueue::Upload() {
  if( !mQueue.isEmpty() ) {
    if( mQueue.size() == 1 ) {
      LOG( "Upload result..." );
    } else {
      LOG( "Found an old result. Uploading that first..." );
    }
    QJsonObject result = mQueue.takeAt( 0 ).toObject();

    mWebProfile.UploadResult( result );
  }
}

void ResultQueue::Check() {
  if( mUploadTimer->isActive() ) {
    // Upload works, nothing to be done
    return;
  }

  Upload();
}
