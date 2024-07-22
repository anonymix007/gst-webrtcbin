async function main() {
  const response = await fetch("/iceServers", {
    method: "post",
    headers: {
      Accept: "application/json, text/plain, */*",
      "Content-Type": "application/json",
    },
    body: null,
  });
  const servers = await response.json();
  kmp(servers);
}

main();

function kmp(iceServers) {
  let activeVideos = 0;
  let pc = new RTCPeerConnection({
    iceServers: iceServers,
  });

  window.audioContext = undefined;

  pc.onicecandidate = function (event) {
	  console.log(event);
  }

  pc.ontrack = function (event) {
    if (event.track.kind === "audio") {
      console.log("Got audio track!", event.track);
      const audio = new Audio();
      audio.srcObject = new MediaStream([event.track]);
      //audio.autoplay = true
    } else if (event.track.kind === "video") {
      console.log("Got video track!", event.track);
      remoteVideo.srcObject = event.streams[0];
    } else {
      console.log("Got unknown track!", event.track);
    }
  };

  let resolutionMap = {
    screenWidth: 0,
    screenHeight: 0,
    canvasWidth: 0,
    canvasHeight: 0,
  };

  let doSignaling = (method) => {
    pc.createOffer({ offerToReceiveVideo: true, offerToReceiveAudio: true })
      .then((offer) => {
        pc.setLocalDescription(offer);
        return fetch(`/${method}`, {
          method: "post",
          headers: {
            Accept: "application/json, text/plain, */*",
            "Content-Type": "application/json",
          },
          body: JSON.stringify(offer),
        });
      })
      .then((res) => res.json())
      .then((res) => pc.setRemoteDescription(res))
      .catch(alert);
  };
  // Create a noop DataChannel. By default PeerConnections do not connect
  // if they have no media tracks or DataChannels

  function sendDataMessage(command, data) {
    if (dataChannel) {
      // Send cordinates
      dataChannel.send(
        JSON.stringify({
          command: command,
          data: data,
        })
      );
    }
  }

  pc.createDataChannel("noop");
  doSignaling("createPeerConnection");

  startVideo = () => {
    dataChannel = pc.createDataChannel("messages");
    dataChannel.onopen = function (event) {
      enableMouseEvents(dataChannel);
      // Fetch screen size from server
      sendDataMessage("screensize", {});
    };
    dataChannel.onmessage = function (event) {
      try {
        const message = JSON.parse(event.data);
        switch (message.command) {
          case "screensize":
            resolutionMap.screenHeight = message.data.height;
            resolutionMap.screenWidth = message.data.width;
            break;
          case "mousepose":
            console.log(message);
            break;
        }
      } catch (e) {
        console.error(e);
      }
    };
    if (pc.getTransceivers().length < 1) {
      pc.addTransceiver("video");
      pc.addTransceiver("audio");
    }
    doSignaling("startVideo");
  };

  stopVideo = () => {
    if (dataChannel != null) {
      dataChannel.close();
      dataChannel = null;
      doSignaling("stopVideo");
    }
  };

  processStartStop = (v) => {
    if (window.audioContext == undefined) {
      window.audioContext = new AudioContext();
    }
    var span = v.currentTarget.querySelector(".material-symbols-outlined");
    if (span.innerText == "login") {
      console.log("Connecting...");
      span.innerText = "logout";
      startVideo();
    } else if (span.innerText == "logout") {
      console.log("Disconnecting...");
      span.innerText = "login";
      stopVideo();
    }
    // TODO: find a better solution
    v.currentTarget.remove();
  };

  const remoteVideo = document.getElementById("remote-video");
  const remoteCanvas = document.getElementById("remote-canvas");
  // Disable right click context on canvas
  remoteCanvas.oncontextmenu = function (e) {
    e.preventDefault();
  };

  function resizeCanvas(canvas, video) {
    const w = video.offsetWidth;
    const h = video.offsetHeight;
    canvas.width = w;
    canvas.height = h;

    resolutionMap.canvasHeight = h;
    resolutionMap.canvasWidth = w;
  }

  remoteVideo.onplaying = () => {
    setInterval(() => {
      resizeCanvas(remoteCanvas, remoteVideo);
    }, 1000);
  };
}
