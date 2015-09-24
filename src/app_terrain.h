#include "def.h"
#include "util.h"
#include "intersect.h"

ray_t get_primary_ray(_in(vec3) cam_local_point, _inout(vec3) cam_origin, _inout(vec3) cam_look_at)
{
	vec3 fwd = normalize(cam_look_at - cam_origin);
	vec3 up = vec3(0, 1, 0);
	vec3 right = cross(up, fwd);
	up = cross(fwd, right);

	return ray_t _begin
		cam_origin,
		normalize(fwd + up * cam_local_point.y + right * cam_local_point.x)
	_end;
}

bool isect_sphere(_in(ray_t) ray, _in(sphere_t) sphere, _inout(float) t0, _inout(float) t1)
{
	vec3 rc = sphere.origin - ray.origin;
	float radius2 = sphere.radius * sphere.radius;
	float tca = dot(rc, ray.direction);
//	if (tca < 0.) return false;
	float d2 = dot(rc, rc) - tca * tca;
	if (d2 > radius2) return false;
	float thc = sqrt(radius2 - d2);
	t0 = tca - thc;
	t1 = tca + thc;

	return true;
}

const vec3 betaR = vec3(5.5e-6, 13.0e-6, 22.4e-6); // Rayleigh scattering coefficients at sea level (m)
const vec3 betaM = vec3(21e-6); // Mie scattering coefficients at sea level (m)
const float hR = 7994.0; // Rayleigh scale height (m)
const float hM = 1200.0; // Mie scale height (m)
const float earth_radius = 6360e3; // (m)
const float atmosphere_radius = 6420e3; // (m)
vec3 sun_dir = vec3(0, 1, 0);
const float sun_power = 20.0;
const float g = 0.76; // defines if the light is mainly scattered along the forward or backwards direction

const int air = 1;
const sphere_t atmosphere = sphere_t _begin
	vec3(0), atmosphere_radius, air
_end;

const int num_samples = 16;
const int num_samples_light = 8;

bool get_sun_light(
	_in(ray_t) ray,
	_inout(float) optical_depthR,
	_inout(float) optical_depthM
){
	float t0, t1;
	isect_sphere(ray, atmosphere, t0, t1);

	float march_pos = 0.;
	float march_step = t1 / float(num_samples_light);

	for (int i = 0; i < num_samples_light; i++) {
		vec3 sample =
			ray.origin +
			ray.direction * (march_pos + 0.5 * march_step);
		float height = length(sample) - earth_radius;
		if (height < 0.)
			return false;

		optical_depthR += exp(-height / hR) * march_step;
		optical_depthM += exp(-height / hM) * march_step;

		march_pos += march_step;
	}

	return true;
}

vec3 get_incident_light(_in(ray_t) ray)
{
	float t0, t1;
	if (!isect_sphere(
		ray, atmosphere, t0, t1)) {
		return vec3(0);
	}

	float march_step = t1 / float(num_samples);

	// cosine angle view and light direction
	float mu = dot(ray.direction, sun_dir);

	// R phase function
	float phaseR =
		3. / (16. * PI) *
		(1. + mu * mu);

	// Mie phase function
	// TODO: replace with Schlick’s 
	float phaseM =
		3. / (8. * PI) *
		((1. - g * g) * (1. + mu * mu)) /
		((2. + g * g)
		* pow(1. + g * g - 2. * g * mu, 1.5));

	// optical depth = average density
	// TODO: wiki
	float optical_depthR = 0.;
	float optical_depthM = 0.;

	vec3 sumR = vec3(0);
	vec3 sumM = vec3(0);
	float march_pos = 0.;

	for (int i = 0; i < num_samples; i++) {
		vec3 sample =
			ray.origin +
			ray.direction * (march_pos + 0.5 * march_step);
		float height = length(sample) - earth_radius;

		// height scale
		// TODO: explain 
		float hr = exp(-height / hR) * march_step;
		float hm = exp(-height / hM) * march_step;

		optical_depthR += hr;
		optical_depthM += hm;

		ray_t light_ray = ray_t _begin
			sample,
			sun_dir
			_end;
		float optical_depth_lightR = 0.;
		float optical_depth_lightM = 0.;
		bool overground = get_sun_light(
			light_ray,
			optical_depth_lightR,
			optical_depth_lightM);

		if (overground) {
			vec3 tau =
				betaR *
				(optical_depthR + optical_depth_lightR) +
				betaM * 1.1 * (optical_depthM + optical_depth_lightM);

			vec3 attenuation = exp(-tau);

			sumR += hr * attenuation;
			sumM += hm * attenuation;
		}

		march_pos += march_step;
	}

	return
		sun_power *
		(sumR * phaseR * betaR +
		sumM * phaseM * betaR);
}

void mainImage(_out(vec4) fragColor, _in(vec2) fragCoord)
{
	vec2 aspect_ratio = vec2(u_res.x / u_res.y, 1);
	float fov = tan(radians(45.0));
	vec2 point_ndc = fragCoord.xy / u_res.xy;
	vec3 point_cam = vec3((2.0 * point_ndc - 1.0) * aspect_ratio * fov, -1.0);

	vec3 col = vec3(0);

	// sun
	//mat3 rot = rotate_around_z(-sin(u_time) * 90.);
	//sun_dir *= rot;

#if 1
	// sky dome angles
	// TODO: understand better
	vec3 p = point_cam;
	float z2 = p.x * p.x + p.y * p.y; // TODO: what about the check <= 1. ?
	float phi = atan(p.y, p.x); // this is actually atan2 from C
	float theta = acos(1.0 - z2);
	vec3 dir = vec3(
		sin(theta) * cos(phi),
		cos(theta),
		sin(theta) * sin(phi));

	ray_t ray = ray_t _begin
		vec3(0, earth_radius + 1., 0),
		dir
	_end;
	
	col = get_incident_light(ray);
#else

	vec3 eye = vec3 (0, earth_radius + 1., 0);
	vec3 look_at = vec3 (0, earth_radius + 1.5, -1);
	
	ray_t ray = get_primary_ray(point_cam, eye, look_at);
	
	plane_t terrain = plane_t _begin
		vec3 (0, -1, 0),
		earth_radius,
		0
	_end;
	
	hit_t hit = no_hit;
	intersect_plane (ray, terrain, hit);
	
	if (hit.t > max_dist) {
		col = get_incident_light(ray);
	} else {
		col = hit.material_param * vec3 (0.333);
	}
#endif

//    col = corect_gamma(col, 2.25);
	fragColor = vec4(col, 1);
}