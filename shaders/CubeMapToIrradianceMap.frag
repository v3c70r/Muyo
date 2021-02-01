#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 localPos;
layout(location = 0) out vec4 outIrradiance;

layout(set = 1, binding = 0) uniform samplerCube environmentMap;

void main()
{
    // the sample direction equals the hemisphere's orientation
    const float PI = 3.14159265359;
    vec3 normal = normalize(localPos);

    vec3 irradiance = vec3(0.0);
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = cross(up, normal);
    up = cross(normal, right);

    float sampleDelta = 0.025;
    float nrSamples = 0.0;
    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            // spherical to cartesian (in tangent space)
            vec3 tangentSample =
                vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            // tangent space to world
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up +
                             tangentSample.z * normal;

            irradiance += texture(environmentMap, sampleVec).rgb * cos(theta) *
                          sin(theta);
            nrSamples++;
        }
    }

    outIrradiance = vec4(PI * irradiance / nrSamples, 1.0);

    //vec3 envColor = texture(environmentMap, localPos).rgb;

    //envColor = envColor / (envColor + vec3(1.0));
    //envColor = pow(envColor, vec3(1.0 / 2.2));

    //outIrradiance = vec4(envColor, 1.0);
}
