import socket
import queue
import numpy as np
import time

def ReceiveData(packetInit: dict, q: queue):
    localIP = "127.0.0.1"
    localPort = 12000
    bufferSize = 60000

    UDPServerSocket = socket.socket(family=socket.AF_INET, type=socket.SOCK_DGRAM)

    UDPServerSocket.bind((localIP, localPort))
    # timeout = 5
    # UDPServerSocket.settimeout(timeout)
    
    packetDict = {}

    while True:
        bytesAddressPair = UDPServerSocket.recvfrom(bufferSize)

        packet = bytesAddressPair[0]

        frame = int.from_bytes(packet[0:4], "little")
        count = int.from_bytes(packet[4:8], "little")

        if frame == 0xFFFFFFFF:  # initial packet
            # to do
            # modify initial packet
            packetInit["packetNum"] = int.from_bytes(packet[4:8], "little")
            packetInit["bytesPoints"] = int.from_bytes(packet[8:12], "little")
            packetInit["bytesDepthmap"] = int.from_bytes(packet[12:16], "little")
            packetInit["bytesRGBmap"] = int.from_bytes(packet[16:20], "little")
            packetInit["numLidars"] = int.from_bytes(packet[20:24], "little")
            packetInit["lidarRes"] = int.from_bytes(packet[24:28], "little")
            packetInit["lidarChs"] = int.from_bytes(packet[28:32], "little")
            packetInit["imageWidth"] = int.from_bytes(packet[32:36], "little")
            packetInit["imageHeight"] = int.from_bytes(packet[36:40], "little")

            print("Num Packets : {}".format(packetInit["packetNum"]))
            print("Bytes of Points : {}".format(packetInit["bytesPoints"]))
            print("Bytes of RGB map : {}".format(packetInit["bytesDepthmap"]))
            print("Bytes of Depth map : {}".format(packetInit["bytesRGBmap"]))
            print("Num Lidars : {}".format(packetInit["numLidars"]))
            print("Lidar Resolution : {}".format(packetInit["lidarRes"]))
            print("Lidar Channels : {}".format(packetInit["lidarChs"]))
            print("Camera Width : {}".format(packetInit["imageWidth"]))
            print("Camera Height : {}".format(packetInit["imageHeight"]))
        else:
            # print("frame : ", frame)
            # print("packet count : ", count)
            if frame not in packetDict:
                packetDict[frame] = {}
            packetDict[frame][count] = packet[8:]
            #print("count sum : ", len(packetDict[frame]))
            for key in list(packetDict.keys()):
                if len(packetDict[key]) == packetInit["packetNum"]:
                    fullPackets = b"".join([packetDict[frame][i] for i in range(packetInit["packetNum"])])
                    #print("queue size : ", q.qsize())
                    if q.qsize() > 9 :
                        q.get()
                    #print(frame)
                    #print("dic len defor : " , len(packetDict))
                    #print("send : ", key)
                    q.put(np.array([key], dtype=np.int32).tobytes() + fullPackets)
                    del packetDict[key]
                    #print("dic len after : " , len(packetDict))
                    time.sleep(0.003)
                #else : 
                    # no full packet
                    #continue