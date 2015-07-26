#version 330

/********************************************************************************
* OpenGL-Framework                                                              *
* Copyright (c) 2015 Daniel Chappuis                                            *
*********************************************************************************
*                                                                               *
* This software is provided 'as-is', without any express or implied warranty.   *
* In no event will the authors be held liable for any damages arising from the  *
* use of this software.                                                         *
*                                                                                *
* Permission is granted to anyone to use this software for any purpose,         *
* including commercial applications, and to alter it and redistribute it        *
* freely, subject to the following restrictions:                                *
*                                                                               *
* 1. The origin of this software must not be misrepresented; you must not claim *
*    that you wrote the original software. If you use this software in a        *
*    product, an acknowledgment in the product documentation would be           *
*    appreciated but is not required.                                           *
*                                                                               *
* 2. Altered source versions must be plainly marked as such, and must not be    *
*    misrepresented as being the original software.                             *
*                                                                               *
* 3. This notice may not be removed or altered from any source distribution.    *
*                                                                               *
********************************************************************************/

// Uniform variables
uniform vec3 lightAmbientColor;             // Lights ambient color
uniform vec3 light0PosCameraSpace;          // Camera-space position of the light
uniform vec3 light0DiffuseColor;            // Light 0 diffuse color
uniform vec3 light0SpecularColor;           // Light 0 specular color
uniform float shininess;                    // Shininess
uniform sampler2D textureSampler;           // Texture
uniform sampler2D shadowMapSampler;         // Shadow map texture sampler
uniform bool isTexture;                     // True if we need to use the texture
uniform vec4 vertexColor;                   // Vertex color

// In variables
in vec3 vertexPosCameraSpace;          // Camera-space position of the vertex
in vec3 vertexNormalCameraSpace;       // Vertex normal in camera-space
in vec2 texCoords;                     // Texture coordinates
in vec4 shadowMapCoords;                // Shadow map texture coords

// Out variable
out vec4 color;                        // Output color

void main() {

    // Compute the ambient term
    vec3 ambient = lightAmbientColor;

    // Get the texture color
    vec3 textureColor = vertexColor.rgb;
    if (isTexture) textureColor = texture(textureSampler, texCoords).rgb;

    // Compute the surface normal vector
    vec3 N = normalize(vertexNormalCameraSpace);

    // Compute the diffuse term of light 0
    vec3 L0 = normalize(light0PosCameraSpace - vertexPosCameraSpace);
    float diffuseFactor = max(dot(N, L0), 0.0);
    vec3 diffuse = light0DiffuseColor * diffuseFactor * textureColor;

    // Compute the specular term of light 0
    vec3 V = normalize(-vertexPosCameraSpace);
    vec3 H0 = normalize(V + L0);
    float specularFactor = pow(max(dot(N, H0), 0.0), shininess);
    if (diffuseFactor < 0.0) specularFactor = 0.0;
    vec3 specular = light0SpecularColor * specularFactor;

    // Compute shadow factor
    vec4 shadowMapCoordsOverW = shadowMapCoords / shadowMapCoords.w ;
    //shadowMapCoordsOverW += 0.0005;
    float distanceInShadowMap = texture(shadowMapSampler, shadowMapCoordsOverW.xy).r;
    float shadow = 0.0;
    if (shadowMapCoords.w > 0) {
        shadow = distanceInShadowMap < shadowMapCoordsOverW.z ? 0.0 : 1.0;
    }

    // Compute the final color
    color = vec4(ambient + shadow * vertexColor.rgb, 1.0);
}
