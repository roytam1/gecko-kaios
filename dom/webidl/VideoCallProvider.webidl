/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

[Pref="dom.telephony.enabled"]
interface VideoCallProvider : EventTarget {
  /**
   * To specify which camera to open.
   * @param camera
   *        one of the camera identifiers returned by CameraManager::getListOfCameras()
   *        To close camera if parameter camera is not given.
   * ETA 3/3
   */
  [Throws]
  Promise<void> setCamera(optional DOMString camera);

  /**
   * To get preview media stream.
   # ETA 3/3
   */
  [Throws]
  Promise<SurfaceControl> getPreviewStream(optional SurfaceConfiguration initialConfiguration);

  /**
   * To get display media stream.
   * ETA 3/3
   */
  [Throws]
  Promise<SurfaceControl> getDisplayStream(optional SurfaceConfiguration initialConfiguration);

  /**
   * To specify device orientation.
   * @param orientation
   *        current device's orientation, one of values 0, 90, 180 and 270.
   * ETA TBD
   */
  [Throws]
  Promise<void> setOrientation(unsigned short orientation);

  /**
   * To zoom in, zoom out the camera.
   * ETA TBD
   */
  [Throws]
  Promise<void> setZoom(float zoom);

  /**
   * To request a session modification.
   * The response will be notified via
   * 1 VideoCallProvider::onsessionmodifyresponse()
   * 2 TelephonyCall::onstatechange()
   * @param from
   *        current video call state
   * @param to
   *         the requested video call
   * ETA TBD
   */
  [Throws]
  Promise<void> sendSessionModifyRequest(VideoCallProfile from, VideoCallProfile to);

  /**
   * To response a media session modification.
   * If you accept the request, network will notifies operation complete via
   * TelephonyCall::onstatechange().
   * @param response
   *        The response video call profile.
   * ETA TBD
   */
  [Throws]
  Promise<void> sendSessionModifyResponse(VideoCallProfile response);

  /**
   * To know current camera's capabilities.
   * The result will be notified via onchangecameracapabilities event.
   * ETA TBD
   */
  [Throws]
  Promise<void> requestCameraCapabilities();

  /**
   * When receiving remote session modification request.
   * Please refer VideoCallSessionModifyRequestEvent for event structure.
   * ETA TBD
   */
  attribute EventHandler onsessionmodifyrequest;

  /**
   * When receiving remote session modification response.
   * Please refer VideoCallSessionModifyResponseEvent for event structure.
   */
  attribute EventHandler onsessionmodifyresponse;

  /**
   * When receiving video call related events.
   * Please refer VideoCallSessionChangeEvent for event structure.
   */
  attribute EventHandler oncallsessionevent;

  /**
   * When receiving remote dimensions changed event.
   * UI may need to adjust display ratio bases on the new dimensions.
   * Please refer VideoCallPeerDimensionsEvent for event structure.
   */
  attribute EventHandler onchangepeerdimensions;

  /**
   * When receiving camera capabilities change event.
   * UI may need to adjust preview ratio bases on the new dimensions.
   * Please refer VideoCallCameraCapabilitiesChangeEvent for event structure.
   */
  attribute EventHandler onchangecameracapabilities;

  /**
   * When receiving video quality change event.
   * Please refer VideoCallQualityEvent for event structure.
   */
  attribute EventHandler onchangevideoquality;
};

/**
 * The possible values of video call quality.
 * Please sync with nsIVideoCallProfile:QUALITY_*.
 */
enum VideoCallQuality
{
  "unknown", "high", "medium", "low", "default"
};

/**
 * The possible values of video call state.
 * Please sync with nsIVideoCallProfile:STATE_*.
 */
enum VideoCallState
{
  "audio-only",
  "tx-enabled",
  "rx-enabled",
  "bidirectional",
  "paused"
};

/**
 * To describe video call detail.
 */
interface VideoCallProfile
{
  readonly attribute VideoCallQuality quality;
  readonly attribute VideoCallState state;
};

interface VideoCallCameraCapabilities
{
  readonly attribute unsigned short width;
  readonly attribute unsigned short height;
  readonly attribute boolean zoomSupported;
  readonly attribute unsigned short maxZoom;
};