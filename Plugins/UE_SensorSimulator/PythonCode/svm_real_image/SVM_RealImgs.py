import json
import os

import cv2 as cv
import numpy as np
import panda3d.core as p3d
from direct.filter.FilterManager import FilterManager
from direct.showbase.ShowBase import ShowBase
from direct.task import Task
from panda3d.core import Shader

still_shot_mode = False  # Set this variable to True or False to enable or disable still shot mode

with open("./config_raymarine.json") as f:
    calib = json.load(f)

boat_breadth = calib["boat_type"]["custom_breadth"] * 100
boat_length = calib["boat_type"]["custom_length"] * 100

extrinsic_parameter = calib["extrinsic_parameter"]["cameras"]
del extrinsic_parameter[1:3]
extrinsic_parameter[2], extrinsic_parameter[3] = extrinsic_parameter[3], extrinsic_parameter[2]
for extrinsic in extrinsic_parameter:
    extrinsic["location"]["translation_x"] *= 100
    extrinsic["location"]["translation_y"] *= 100
    extrinsic["location"]["translation_z"] *= 100

K1 = 1.281584985127447
K2 = 0.170043067138006
K3 = -0.023341058557079
K4 = 0.007690791651144
K5 = -0.001380968639013

img_width = 1920
img_height = 1080
fx = 345.12136354806347
fy = 346.09009197978003
cx = 959.5
cy = 539.5
skew_c = 0

winSizeX = 1024
winSizeY = 1024


class SurroundView(ShowBase):
    def __init__(self):
        super().__init__()

        winprops = p3d.WindowProperties()
        winprops.setSize(winSizeX, winSizeY)
        self.win.requestProperties(winprops)

        self.render.setAntialias(p3d.AntialiasAttrib.MAuto)
        self.cam.setPos(0, 0, -4000)
        self.cam.lookAt(p3d.LPoint3f(0, 0, 0), p3d.LVector3f(1, 0, 0))

        mat = p3d.Mat4(self.cam.getMat())
        mat.invertInPlace()
        self.mouseInterfaceNode.setMat(mat)
        self.enableMouse()

        self.cam.setPos(0, 0, 0)
        self.cam.setHpr(0, 0, 0)

        self.renderObj = p3d.NodePath("fgRender")
        self.renderSVM = p3d.NodePath("bgRender")

        # Set up the offscreen buffer
        fb_props = p3d.FrameBufferProperties()
        # fb_props.setRgbColor(True)
        fb_props.set_rgba_bits(32, 32, 32, 32)
        fb_props.setAuxRgba(2)  # Set up one auxiliary buffer
        fb_props.setDepthBits(24)

        # self.buffer1 = self.win.makeTextureBuffer("buffer1", winSizeX, winSizeY, to_ram=True, fbp=fb_props)
        self.buffer1 = self.graphics_engine.make_output(
            self.pipe,
            "offscreen buffer",
            -20,
            fb_props,
            winprops,
            p3d.GraphicsPipe.BF_refuse_window,
            self.win.get_gsg(),
            self.win,
        )  # DO NOT MAKE BUFFER via base.win.makeTextureBuffer for readback-purposed render target (F_rgba32)
        # base.win.makeTextureBuffer create a default render target buffer (F_rgba)
        self.buffer1.setClearColor(p3d.Vec4(0, 0, 0, 0))
        # self.buffer1.setSort(10)

        # Create the render textures
        tex1 = p3d.Texture()  # self.buffer1.getTexture()
        tex2 = p3d.Texture()
        tex1.set_format(p3d.Texture.F_rgba32i)
        tex2.set_format(p3d.Texture.F_rgba32i)
        # tex1.set_component_type(p3d.Texture.T_unsigned_int)
        # tex2.set_component_type(p3d.Texture.T_unsigned_int)
        tex1.setClearColor(p3d.Vec4(0, 0, 0, 0))
        tex2.setClearColor(p3d.Vec4(0, 0, 0, 0))
        tex1.clear()
        tex2.clear()
        self.buffer1.addRenderTexture(
            tex1, p3d.GraphicsOutput.RTM_bind_or_copy | p3d.GraphicsOutput.RTM_copy_ram, p3d.GraphicsOutput.RTP_color
        )
        # I dont know why the RTP_aux_rgba_x with RTM_copy_ram (F_rgba32i?!) affects incorrect render-to-texture result.
        # so, tricky.. the tex2 only contains pos info (no need to readback to RAM)
        self.buffer1.addRenderTexture(
            tex2,  # p3d.GraphicsOutput.RTM_bind_or_copy |
            p3d.GraphicsOutput.RTM_bind_or_copy,
            p3d.GraphicsOutput.RTP_aux_rgba_0,
        )

        # Set up the offscreen camera
        self.cam1 = self.makeCamera(self.buffer1, scene=self.renderSVM, lens=self.cam.node().getLens())
        self.cam1.reparentTo(self.cam)

        # Set up a buffer for the first pass
        fb_props2 = p3d.FrameBufferProperties()
        fb_props2.setRgbColor(True)
        fb_props2.set_rgba_bits(8, 8, 8, 8)
        # fb_props2.setAuxRgba(1)  # Set up one auxiliary buffer
        fb_props2.setDepthBits(24)
        self.buffer2 = self.graphics_engine.make_output(
            self.pipe,
            "offscreen buffer2",
            -1,
            fb_props2,
            winprops,
            p3d.GraphicsPipe.BF_refuse_window,
            self.win.get_gsg(),
            self.win,
        )
        # self.buffer2 = self.win.makeTextureBuffer("buffer2", winSizeX, winSizeY) # this includes a default render target RTP_color
        texP0 = p3d.Texture()
        texP0.set_format(p3d.Texture.F_rgba)
        texP0.set_component_type(p3d.Texture.T_unsigned_byte)
        self.buffer2.addRenderTexture(texP0, p3d.GraphicsOutput.RTM_bind_or_copy, p3d.GraphicsOutput.RTP_color)

        self.buffer2.setClearColor(p3d.Vec4(0, 0, 0, 1))
        # self.buffer2.setSort(0)

        # Set up a camera for the first pass
        self.cam2 = self.makeCamera(self.buffer2, scene=self.renderObj, lens=self.cam.node().getLens())
        self.cam2.reparentTo(self.cam)

        alight = p3d.AmbientLight("alight")
        alight.setColor((0.2, 0.2, 0.2, 1))
        alnp = self.renderObj.attachNewNode(alight)
        self.renderObj.setLight(alnp)

        plight = p3d.PointLight("plight")
        plight.setColor((0.8, 0.8, 0.8, 1))
        plnp = self.renderObj.attachNewNode(plight)
        plnp.setPos(0, 0, -4000)
        self.renderObj.setLight(plnp)

        self.boat = self.loader.loadModel("./src/avikus_boat.glb")
        # self.boat.setHpr(90, -90, 180)
        self.boat.setHpr(90, 0, 180)

        bbox = self.boat.getTightBounds()
        scale_x = boat_length / (bbox[1].x - bbox[0].x)
        scale_y = boat_breadth / (bbox[1].y - bbox[0].y)
        self.boat.setScale(scale_y, scale_x, scale_y)  # (breath, length, height)

        bbox = self.boat.getTightBounds()
        origin = (bbox[0] + bbox[1]) / 2
        self.boat.setPos(-origin.x, -origin.y, 0)
        self.boat.reparentTo(self.renderObj)

        self.axis = self.loader.loadModel("zup-axis")
        self.axis.setPos(0, 0, 0)
        self.axis.setScale(100)
        self.axis.reparentTo(self.renderObj)

        # Set the debugMode variable to control the behavior of the script
        # 0: Default behavior
        # 1: Inpainting will not be performed; instead, the blending area will be visualized
        # 2: Inpainting will not be performed; instead, risk factors will be highlighted
        self.debug_mode = 0

        self.manager = FilterManager(self.win, self.cam)
        texInterResult = p3d.Texture()
        self.interquad = self.manager.renderQuadInto(colortex=None)  # make dummy texture... for post processing...
        self.finalquad = self.manager.renderSceneInto(colortex=None)
        self.manager.buffers[0].clearRenderTextures()
        self.manager.buffers[0].addRenderTexture(
            texInterResult,
            p3d.GraphicsOutput.RTM_bind_or_copy | p3d.GraphicsOutput.RTM_copy_ram,
            p3d.GraphicsOutput.RTP_color,
        )
        self.interquad.setShader(
            Shader.load(Shader.SL_GLSL, vertex="glsl/post1_vs.glsl", fragment="glsl/svm_post1_ps_real.glsl")
        )
        self.finalquad.setShader(
            Shader.load(Shader.SL_GLSL, vertex="glsl/post1_vs.glsl", fragment="glsl/final_composite_ps_real.glsl")
        )

        self.interquad.setShaderInput("texGeoInfo0", self.buffer1.getTexture(0))
        self.interquad.setShaderInput("texGeoInfo1", self.buffer1.getTexture(1))
        self.interquad.setShaderInput("texGeoInfo2", self.buffer2.getTexture(0))

        self.finalquad.setShaderInput("texGeoInfo2", self.buffer2.getTexture(0))

        self.texInpaint = p3d.Texture()

        self.interquad.setShaderInput("w01", 0.5)
        self.interquad.setShaderInput("w12", 0.5)
        self.interquad.setShaderInput("w23", 0.5)
        self.interquad.setShaderInput("w30", 0.5)

        self.interquad.setShaderInput("debug_mode", self.debug_mode)

        self.GeneratePlaneNode()
        self.plane.reparentTo(self.renderSVM)

        self.plane.setShaderInput("K1_", K1)
        self.plane.setShaderInput("K2_", K2)
        self.plane.setShaderInput("K3_", K3)
        self.plane.setShaderInput("K4_", K4)
        self.plane.setShaderInput("K5_", K5)

        self.plane.setShaderInput("img_width_", img_width)
        self.plane.setShaderInput("img_height_", img_height)

        self.plane.setShaderInput("fx_", fx)
        self.plane.setShaderInput("fy_", fy)
        self.plane.setShaderInput("cx_", cx)
        self.plane.setShaderInput("cy_", cy)

        self.interquad.setShaderInput("K1_", K1)
        self.interquad.setShaderInput("K2_", K2)
        self.interquad.setShaderInput("K3_", K3)
        self.interquad.setShaderInput("K4_", K4)
        self.interquad.setShaderInput("K5_", K5)

        self.interquad.setShaderInput("img_width_", img_width)
        self.interquad.setShaderInput("img_height_", img_height)

        self.interquad.setShaderInput("fx_", fx)
        self.interquad.setShaderInput("fy_", fy)
        self.interquad.setShaderInput("cx_", cx)
        self.interquad.setShaderInput("cy_", cy)

        self.accept("r", self.shaderRecompile)

        self.bufferViewer.setPosition("llcorner")
        self.bufferViewer.setCardSize(0.5, 0)
        self.accept("v", self.bufferViewer.toggleEnable)

        self.accept("1", self.setDebugMode, [0])
        self.accept("2", self.setDebugMode, [1])
        self.accept("3", self.setDebugMode, [2])

        self.taskMgr.add(self.readTextureData, "readTextureData")
        self.buffer1.set_active(False)
        self.buffer2.set_active(False)

    def setDebugMode(self, mode):
        self.debug_mode = mode
        self.interquad.setShaderInput("debug_mode", mode)

    def GeneratePlaneNode(self):
        # shader setting for SVM
        self.planeShader = Shader.load(
            Shader.SL_GLSL, vertex="glsl/svm_vs.glsl", fragment="glsl/svm_ps_plane_real.glsl"
        )
        vdata = p3d.GeomVertexData("triangle_data", p3d.GeomVertexFormat.getV3t2(), p3d.Geom.UHStatic)
        vdata.setNumRows(4)  # optional for performance enhancement!
        vertex = p3d.GeomVertexWriter(vdata, "vertex")
        texcoord = p3d.GeomVertexWriter(vdata, "texcoord")

        bbox = self.boat.getTightBounds()
        # print(bbox)
        self.waterZ = 0
        waterPlaneLength = 1000
        vertex.addData3(-waterPlaneLength, -waterPlaneLength, self.waterZ)
        vertex.addData3(waterPlaneLength, -waterPlaneLength, self.waterZ)
        vertex.addData3(waterPlaneLength, waterPlaneLength, self.waterZ)
        vertex.addData3(-waterPlaneLength, waterPlaneLength, self.waterZ)
        # not use...
        texcoord.addData2(0, 1)
        texcoord.addData2(1, 1)
        texcoord.addData2(1, 0)
        texcoord.addData2(0, 0)

        primTris = p3d.GeomTriangles(p3d.Geom.UHStatic)
        primTris.addVertices(0, 1, 2)
        primTris.addVertices(0, 2, 3)

        geom = p3d.Geom(vdata)
        geom.addPrimitive(primTris)
        geomNode = p3d.GeomNode("SVM Plane")
        geomNode.addGeom(geom)
        # note nodePath is the instance for node (obj resource)
        self.plane = p3d.NodePath(geomNode)
        self.plane.setTwoSided(True)
        self.plane.setShader(self.planeShader)

        # the following mat4 array does not work...
        # matViewProjs = [p3d.LMatrix4f(), p3d.LMatrix4f(), p3d.LMatrix4f(), p3d.LMatrix4f()]
        # self.plane.setShaderInput("matViewProjs", matViewProjs)

        # self.planeTexs = [p3d.Texture(), p3d.Texture(), p3d.Texture(), p3d.Texture()]
        for i in range(4):
            self.plane.setShaderInput("matViewProj" + str(i), p3d.Mat4())
            self.interquad.setShaderInput("matViewProj" + str(i), p3d.Mat4())

        self.planeTexArray = p3d.Texture()
        self.planeTexArray.setup2dTextureArray(
            img_width, img_height, 4, p3d.Texture.T_unsigned_byte, p3d.Texture.F_rgba
        )
        self.plane.setShaderInput("cameraImgs", self.planeTexArray)
        self.interquad.setShaderInput("cameraImgs", self.planeTexArray)

        self.camPositions = [p3d.LVector4f(), p3d.LVector4f(), p3d.LVector4f(), p3d.LVector4f()]
        self.plane.setShaderInput("camPositions", self.camPositions)

        # initial setting like the above code! (for resource optimization)
        # self.semanticTexs = [p3d.Texture(), p3d.Texture(), p3d.Texture(), p3d.Texture()]
        self.semanticTexArray = p3d.Texture()
        self.semanticTexArray.setup2dTextureArray(img_width, img_height, 4, p3d.Texture.T_int, p3d.Texture.F_r32i)
        # print(img_width, img_height)
        self.plane.setShaderInput("semanticImgs", self.semanticTexArray)
        self.interquad.setShaderInput("semanticImgs", self.semanticTexArray)

    def readTextureData(self, task):
        self.buffer1.set_active(True)
        self.buffer2.set_active(True)
        self.win.set_active(False)
        self.graphics_engine.render_frame()

        # Get the render target texture
        tex0 = self.buffer1.getTexture(0)
        # Read the texture data into a PTAUchar
        tex0_data = tex0.get_ram_image()
        # Convert the PTAUchar to a NumPy array
        np_texture0 = np.frombuffer(tex0_data, np.uint32)
        # Reshape the NumPy array to match the texture dimensions and number of channels
        np_texture0 = np_texture0.reshape((tex0.get_y_size(), tex0.get_x_size(), 4))

        # to do with array0, array1
        array0 = np_texture0.copy()
        tex0.setRamImage(array0)
        self.interquad.setShaderInput("texGeoInfo0", tex0)

        # dynamic blending weight
        mapProp = np.flip(array0[:, :, 2], 0)
        overlapIndex0 = np.flip(array0[:, :, 1], 0)
        overlapIndex1 = np.flip(array0[:, :, 0], 0)
        count = np.flip(array0[:, :, 3], 0)

        semantic = mapProp // 100
        camId0 = mapProp % 100 // 10
        camId1 = mapProp % 10
        overlapIndex0 = np.where(count == 2, overlapIndex0, 255)
        overlapIndex1 = np.where(count == 2, overlapIndex1, 255)

        cam0 = np.where((overlapIndex0 == 0) | (overlapIndex1 == 0), 1, 0)
        cam1 = np.where((overlapIndex0 == 1) | (overlapIndex1 == 1), 1, 0)
        cam2 = np.where((overlapIndex0 == 2) | (overlapIndex1 == 2), 1, 0)
        cam3 = np.where((overlapIndex0 == 3) | (overlapIndex1 == 3), 1, 0)

        semantic01 = np.where(cam0 & cam1, semantic, 0)
        semantic12 = np.where(cam1 & cam2, semantic, 0)
        semantic23 = np.where(cam2 & cam3, semantic, 0)
        semantic30 = np.where(cam3 & cam0, semantic, 0)

        w = [0.5, 0.5, 0.5, 0.5]

        overlap_semantics = [semantic01, semantic12, semantic23, semantic30]

        # sets blending weights w based on the highest semantic value
        for semantic_value in range(1, np.max(semantic) + 1):
            for i, overlap_semantic in enumerate(overlap_semantics):
                idx0 = i
                idx1 = (i + 1) % 4

                camId0_values = camId0[((camId0 == idx0) | (camId0 == idx1)) & (overlap_semantic == semantic_value)]
                camId1_values = camId1[((camId1 == idx0) | (camId1 == idx1)) & (overlap_semantic == semantic_value)]

                idx0_count = np.sum(camId0_values == idx0) + np.sum(camId1_values == idx0)
                idx1_count = np.sum(camId0_values == idx1) + np.sum(camId1_values == idx1)

                if idx0_count + idx1_count == 0:
                    continue

                w[i] = idx1_count / (idx0_count + idx1_count)

        w = np.clip(w, 0.1, 0.9)

        self.interquad.setShaderInput("w01", w[0])
        self.interquad.setShaderInput("w12", w[1])
        self.interquad.setShaderInput("w23", w[2])
        self.interquad.setShaderInput("w30", w[3])

        # read-back
        texInterResult = self.manager.buffers[0].getTexture(0)

        if self.debug_mode == 0:
            tex_data = texInterResult.get_ram_image()
            np_texture = np.frombuffer(tex_data, np.uint8)
            np_texture = np_texture.reshape((texInterResult.get_y_size(), texInterResult.get_x_size(), 4))

            # inpainting
            array = np_texture.copy()
            img = cv.cvtColor(array, cv.COLOR_BGRA2BGR)
            mask = cv.inRange(img, (0, 0, 0), (0, 0, 0))
            dst = cv.inpaint(img, mask, 3, cv.INPAINT_TELEA)
            array = cv.cvtColor(dst, cv.COLOR_BGR2BGRA)

            self.texInpaint.setup2dTexture(
                texInterResult.get_x_size(),
                texInterResult.get_y_size(),
                p3d.Texture.T_unsigned_byte,
                p3d.Texture.F_rgba,
            )
            self.texInpaint.setRamImage(array)
            self.finalquad.setShaderInput("texInterResult", self.texInpaint)
        else:
            self.finalquad.setShaderInput("texInterResult", texInterResult)

        self.buffer1.set_active(False)
        self.buffer2.set_active(False)
        self.win.set_active(True)
        return task.cont

    def shaderRecompile(self):
        self.planeShader = Shader.load(
            Shader.SL_GLSL, vertex="glsl/svm_vs.glsl", fragment="glsl/svm_ps_plane_real.glsl"
        )
        self.plane.setShader(self.planeShader)

        self.interquad.setShader(
            Shader.load(Shader.SL_GLSL, vertex="glsl/post1_vs.glsl", fragment="glsl/svm_post1_ps_real.glsl")
        )
        self.finalquad.setShader(
            Shader.load(Shader.SL_GLSL, vertex="glsl/post1_vs.glsl", fragment="glsl/final_composite_ps_real.glsl")
        )


def make_extrinsic_matrix(extrinsic):
    defaultYaw = 0
    # camera_position = int(extrinsic["camera_position"])
    # if camera_position == 1:
    #     print("position is 1")
    #     defaultYaw = 0
    # elif camera_position == 2:
    #     print("position is 2")
    #     defaultYaw = 90
    # elif camera_position == 3:
    #     print("position is 3")
    #     defaultYaw = -90
    # elif camera_position == 4:
    #     print("position is 4")
    #     defaultYaw = 90
    # elif camera_position == 5:
    #     print("position is 5")
    #     defaultYaw = -90
    # elif camera_position == 6:
    #     print("position is 6")
    #     defaultYaw = 180
    rotation = [
        -extrinsic["rotation"]["pitch"],
        extrinsic["rotation"]["roll"],
        -extrinsic["rotation"]["yaw"] - defaultYaw,
    ]
    translation = [
        extrinsic["location"]["translation_x"],
        extrinsic["location"]["translation_y"],
        extrinsic["location"]["translation_z"],
    ]
    rotation_matrix0 = euler_to_matrix(rotation)
    rotation_matrix1 = euler_to_matrix([0, 0, -90])
    rotation_matrix = rotation_matrix1 @ rotation_matrix0
    return translation, rotation_matrix


def euler_to_matrix(euler_angle):
    """
    :param euler_angle: [x,y,z] in degree
    :return: rotation matrix
    """
    # calculate rotation about the x-axis
    R_x = np.array(
        [
            [1.0, 0.0, 0.0],
            [0.0, np.cos(np.deg2rad(-euler_angle[0])), -np.sin(np.deg2rad(-euler_angle[0]))],
            [0.0, np.sin(np.deg2rad(-euler_angle[0])), np.cos(np.deg2rad(-euler_angle[0]))],
        ],
        dtype=float,
    )
    # calculate rotation about the y-axis
    R_y = np.array(
        [
            [np.cos(np.deg2rad(-euler_angle[1])), 0.0, np.sin(np.deg2rad(-euler_angle[1]))],
            [0.0, 1.0, 0.0],
            [-np.sin(np.deg2rad(-euler_angle[1])), 0.0, np.cos(np.deg2rad(-euler_angle[1]))],
        ],
        dtype=float,
    )
    # calculate rotation about the z-axis
    R_z = np.array(
        [
            [np.cos(np.deg2rad(-euler_angle[2])), -np.sin(np.deg2rad(-euler_angle[2])), 0.0],
            [np.sin(np.deg2rad(-euler_angle[2])), np.cos(np.deg2rad(-euler_angle[2])), 0.0],
            [0.0, 0.0, 1.0],
        ],
        dtype=float,
    )
    return R_z @ R_y @ R_x


def computeLookAtRH(eye, center, up):
    z = eye - center
    z.normalize()
    x = up.cross(z)
    x.normalize()
    y = z.cross(x)
    # print(("x {}").format(x))
    # print(("y {}").format(y))
    # print(("z {}").format(z))
    # row major in pand3d core but memory convention is based on the conventional column major
    matLookAt = p3d.LMatrix4f(
        x[0],
        y[0],
        z[0],
        0.0,
        x[1],
        y[1],
        z[1],
        0.0,
        x[2],
        y[2],
        z[2],
        0.0,
        -p3d.LVector3f.dot(x, eye),
        -p3d.LVector3f.dot(y, eye),
        -p3d.LVector3f.dot(z, eye),
        1.0,
    )
    return matLookAt


def make_view_matrix(translation, rotation_matrix):
    T = p3d.LMatrix4f().translateMat(*translation)
    R = p3d.LMatrix4f(p3d.LMatrix3f(*rotation_matrix.flatten()))
    R.transposeInPlace()
    view2world = R * T
    camPos = view2world.xformPoint(p3d.Vec3(0, 0, 0))
    camView = view2world.xformVec(p3d.Vec3(0, 0, 1)).normalized()
    camUp = view2world.xformVec(p3d.Vec3(0, -1, 0)).normalized()
    # print(("camPos {}").format(camPos))
    # print(("camView {}").format(camView))
    # print(("camUp  {}").format(camUp))
    return computeLookAtRH(camPos, camPos + camView, camUp)


def make_projection_matrix(fx, fy, skew_c, cx, cy, img_width, img_height, near_p, far_p):
    q = far_p / (near_p - far_p)
    qn = far_p * near_p / (near_p - far_p)

    projection_matrix = p3d.LMatrix4f()
    projection_matrix[0][0] = 2.0 * fx / img_width
    projection_matrix[1][0] = -2.0 * skew_c / img_width
    projection_matrix[2][0] = (img_width + 2.0 * 0 - 2.0 * cx) / img_width
    projection_matrix[3][0] = 0
    projection_matrix[0][1] = 0
    projection_matrix[1][1] = 2.0 * fy / img_height
    projection_matrix[2][1] = -(img_height + 2.0 * 0 - 2.0 * cy) / img_height
    projection_matrix[3][1] = 0
    projection_matrix[0][2] = 0
    projection_matrix[1][2] = 0
    projection_matrix[2][2] = q
    projection_matrix[3][2] = qn
    projection_matrix[0][3] = 0
    projection_matrix[1][3] = 0
    projection_matrix[2][3] = -1.0
    projection_matrix[3][3] = 0

    # projection_matrix.transposeInPlace()
    return projection_matrix


def InitSVM(base, imageWidth, imageHeight):
    matProj = make_projection_matrix(fx, fy, skew_c, cx, cy, imageWidth, imageHeight, 10, 10000)
    # projMat.transposeInPlace()

    matViewProjs = [p3d.LMatrix4f(), p3d.LMatrix4f(), p3d.LMatrix4f(), p3d.LMatrix4f()]
    for i, extrinsic in enumerate(extrinsic_parameter):
        translation, rotation_matrix = make_extrinsic_matrix(extrinsic)
        matView = make_view_matrix(translation, rotation_matrix)
        matViewProj = matView * matProj
        matViewProjs[i] = matViewProj

        base.plane.setShaderInput("matViewProj" + str(i), matViewProj)
        base.interquad.setShaderInput("matViewProj" + str(i), matViewProj)

    base.planeTexArray.setup2dTextureArray(imageWidth, imageHeight, 4, p3d.Texture.T_unsigned_byte, p3d.Texture.F_rgba)
    base.plane.setShaderInput("cameraImgs", base.planeTexArray)

    base.semanticTexArray.setup2dTextureArray(imageWidth, imageHeight, 4, p3d.Texture.T_int, p3d.Texture.F_r32i)
    base.plane.setShaderInput("semanticImgs", base.semanticTexArray)

    # print("Texture Initialized!")


base_path = "./src/RM_data3"
camera_positions = ["front", "right", "rear", "left"]

caps = []

for position in camera_positions:
    cap = cv.VideoCapture(os.path.join(base_path, position + "_cam.webm"))
    caps.append(cap)


semantic_paths = {
    position: sorted(
        [f for f in os.listdir(os.path.join(base_path, "labels")) if f.startswith(position + "_")],
        key=lambda x: int(x.split("_")[1].split(".")[0]),  # Sort files based on the number in filename
    )
    for position in camera_positions
}

num_images = len(semantic_paths["front"])

current_idx = 0


def loadNextImage(task):
    global current_idx

    imgs = []
    semantics = []

    for i, position in enumerate(camera_positions):
        caps[i].set(cv.CAP_PROP_POS_FRAMES, current_idx)
        _, img = caps[i].read()
        img = cv.resize(img, (img_width, img_height))
        img = cv.cvtColor(img, cv.COLOR_BGR2BGRA)
        imgs.append(img)

        semantic = cv.imread(os.path.join(base_path, "labels", semantic_paths[position][current_idx]))
        semantic = cv.resize(semantic, (img_width, img_height), cv.INTER_NEAREST)
        semantic = cv.cvtColor(semantic, cv.COLOR_BGR2GRAY)
        semantics.append(semantic)

    imgnpArray = np.array(imgs).astype(np.uint8)
    imgArray = imgnpArray.reshape((4, img_width, img_height, 4))
    cameraArray = imgArray[:, :, :, :].copy()
    semanticArray = np.array(semantics).astype(np.int32)

    mySvm.planeTexArray.setRamImage(cameraArray)
    mySvm.semanticTexArray.setRamImage(semanticArray)

    current_idx += 1
    if current_idx >= num_images:
        current_idx = 0  # Reset the index to loop back to the first image

    return Task.cont


def loadDebugImages():
    imgs = []
    semantics = []

    for i, position in enumerate(camera_positions):
        caps[i].set(cv.CAP_PROP_POS_FRAMES, current_idx)
        _, img = caps[i].read()
        img = cv.resize(img, (img_width, img_height))
        img = cv.cvtColor(img, cv.COLOR_BGR2BGRA)
        imgs.append(img)

        semantic = cv.imread(os.path.join(base_path, "labels", semantic_paths[position][current_idx]))
        semantic = cv.resize(semantic, (img_width, img_height), cv.INTER_NEAREST)
        semantic = cv.cvtColor(semantic, cv.COLOR_BGR2GRAY)
        semantics.append(semantic)

    imgnpArray = np.array(imgs).astype(np.uint8)
    imgArray = imgnpArray.reshape((4, img_width, img_height, 4))
    cameraArray = imgArray[:, :, :, :].copy()
    semanticArray = np.array(semantics).astype(np.int32)

    mySvm.planeTexArray.setRamImage(cameraArray)
    mySvm.semanticTexArray.setRamImage(semanticArray)


if __name__ == "__main__":
    mySvm = SurroundView()

    width = mySvm.win.get_x_size()
    height = mySvm.win.get_y_size()
    # print("init w {}, init h {}".format(width, height))

    InitSVM(mySvm, img_width, img_height)

    if still_shot_mode:
        loadDebugImages()
    else:
        mySvm.taskMgr.add(loadNextImage, "LoadNextImageTask")

    mySvm.run()
