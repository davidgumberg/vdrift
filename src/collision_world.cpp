#include "collision_world.h"
#include "collision_contact.h"
#include "tobullet.h"
#include "model.h"
#include "track.h"

struct MyRayResultCallback : public btCollisionWorld::RayResultCallback
{
	MyRayResultCallback(
		const btVector3 & rayFromWorld,
		const btVector3 & rayToWorld,
		const btCollisionObject * exclude) :
		m_rayFromWorld(rayFromWorld),
		m_rayToWorld(rayToWorld),
		m_shapeId(-1),
		m_triangleId(-1),
		m_exclude(exclude)
	{
		// ctor
	}

	btVector3	m_rayFromWorld;//used to calculate hitPointWorld from hitFraction
	btVector3	m_rayToWorld;

	btVector3	m_hitNormalWorld;
	btVector3	m_hitPointWorld;

	int m_shapeId;
	int m_triangleId;
	const btCollisionObject * m_exclude;

	virtual	btScalar addSingleResult(btCollisionWorld::LocalRayResult& rayResult, bool normalInWorldSpace)
	{
		if (rayResult.m_collisionObject == m_exclude) return 1.0;
		
		//caller already does the filter on the m_closestHitFraction
		btAssert(rayResult.m_hitFraction <= m_closestHitFraction);

		m_closestHitFraction = rayResult.m_hitFraction;
		m_collisionObject = rayResult.m_collisionObject;
		
		if(rayResult.m_localShapeInfo)
		{
			m_shapeId = rayResult.m_localShapeInfo->m_shapePart;
			m_triangleId = rayResult.m_localShapeInfo->m_triangleIndex;
		}
		
		if (normalInWorldSpace)
		{
			m_hitNormalWorld = rayResult.m_hitNormalLocal;
		}
		else
		{
			///need to transform normal into worldspace
			m_hitNormalWorld = m_collisionObject->getWorldTransform().getBasis() * rayResult.m_hitNormalLocal;
		}
		
		m_hitPointWorld.setInterpolate3(m_rayFromWorld,m_rayToWorld,rayResult.m_hitFraction);
		return rayResult.m_hitFraction;
	}
};

static btIndexedMesh GetIndexedMesh(const MODEL & model)
{
	const float * vertices;
	int vcount;
	const int * faces;
	int fcount;
	model.GetVertexArray().GetVertices(vertices, vcount);
	model.GetVertexArray().GetFaces(faces, fcount);

	assert(fcount % 3 == 0); //Face count is not a multiple of 3

	btIndexedMesh mesh;
	mesh.m_numTriangles = fcount / 3;
	mesh.m_triangleIndexBase = (const unsigned char *)faces;
	mesh.m_triangleIndexStride = sizeof(int) * 3;
	mesh.m_numVertices = vcount;
	mesh.m_vertexBase = (const unsigned char *)vertices;
	mesh.m_vertexStride = sizeof(float) * 3;
	mesh.m_vertexType = PHY_FLOAT;
	return mesh;
}

COLLISION_WORLD::COLLISION_WORLD(btScalar timeStep, int maxSubSteps) :
	collisiondispatcher(&collisionconfig),
	//collisionbroadphase(btVector3(-5000, -5000, -5000), btVector3(5000, 5000, 5000)),
	world(&collisiondispatcher, &collisionbroadphase, &constraintsolver, &collisionconfig),
	timeStep(timeStep),
	maxSubSteps(maxSubSteps),
	track(0)
{
	world.setGravity(btVector3(0.0, 0.0, -9.81));
	world.setForceUpdateAllAabbs(false);
}

COLLISION_WORLD::~COLLISION_WORLD()
{
	Clear();
}

void COLLISION_WORLD::AddRigidBody(btRigidBody * body)
{
	world.addRigidBody(body);
}

void COLLISION_WORLD::AddAction(btActionInterface * action)
{
	world.addAction(action);
}

void COLLISION_WORLD::RemoveRigidBody(btRigidBody * body)
{
	world.removeRigidBody(body);
}

void COLLISION_WORLD::RemoveAction(btActionInterface * action)
{
	world.removeAction(action);
}

void COLLISION_WORLD::Reset(const TRACK & t)
{
	Clear();
	
	track = &t;
	
	const std::vector<TRACKOBJECT> & trackob = track->GetTrackObjects();
	//std::cerr << "objects: " << trackob.size() << std::endl;
	
	// single batched shape, requires transformed meshes
	if (false)
	{
		btTriangleIndexVertexArray * trackMesh = new btTriangleIndexVertexArray();
		for(std::vector<TRACKOBJECT>::const_iterator i = trackob.begin(); i != trackob.end(); ++i)
		{
			btIndexedMesh mesh = GetIndexedMesh(*i->model);
			trackMesh->addIndexedMesh(mesh);
		}
		
		// can not use QuantizedAabbCompression because of the track size
		btBvhTriangleMeshShape * trackShape = new btBvhTriangleMeshShape(trackMesh, false);
		
		btCollisionObject * trackObject = new btCollisionObject();
		trackObject->setCollisionShape(trackShape);
		trackObject->setUserPointer(0);
		world.addCollisionObject(trackObject);
		
		meshes.push_back(trackMesh);
		shapes.push_back(trackShape);
		objects.push_back(trackObject);
		
		return;
	}
	
	// single compound shape, with mesh instances
	meshes.reserve(trackob.size());
	if (false)
	{
		btCompoundShape * trackShape = new btCompoundShape(true);
		for(std::vector<TRACKOBJECT>::const_iterator i = trackob.begin(); i != trackob.end(); ++i)
		{
			btTransform transform;
			transform.setOrigin(ToBulletVector(i->position));
			transform.setRotation(ToBulletQuaternion(i->rotation));
			
			btTriangleIndexVertexArray * mesh = new btTriangleIndexVertexArray();
			mesh->addIndexedMesh(GetIndexedMesh(*i->model));
			meshes.push_back(mesh);
			
			btBvhTriangleMeshShape * shape = new btBvhTriangleMeshShape(mesh, true);
			trackShape->addChildShape(transform, shape);
		}
		trackShape->createAabbTreeFromChildren();
		
		btCollisionObject * trackObject = new btCollisionObject();
		trackObject->setCollisionShape(trackShape);
		trackObject->setUserPointer(0);
		world.addCollisionObject(trackObject);
		
		return;
	}
	
	// separate track objects
	shapes.reserve(trackob.size());
	objects.reserve(trackob.size());
	if (true)
	{
		for(std::vector<TRACKOBJECT>::const_iterator i = trackob.begin(); i != trackob.end(); ++i)
		{
			btTriangleIndexVertexArray * mesh = new btTriangleIndexVertexArray();
			mesh->addIndexedMesh(GetIndexedMesh(*i->model));
			meshes.push_back(mesh);
			
			btBvhTriangleMeshShape * shape = new btBvhTriangleMeshShape(mesh, true);
			shapes.push_back(shape);
			
			btCollisionObject * object = new btCollisionObject();
			object->setCollisionShape(shape);
			object->setUserPointer((void*)i->surface);
			objects.push_back(object);
			
			btTransform transform;
			transform.setOrigin(ToBulletVector(i->position));
			transform.setRotation(ToBulletQuaternion(i->rotation));
			object->setWorldTransform(transform);
			
			world.addCollisionObject(object);
		}
		
		return;
	}
}

bool COLLISION_WORLD::CastRay(
	const btVector3 & origin,
	const btVector3 & direction,
	const btScalar length,
	const btCollisionObject * caster,
	COLLISION_CONTACT & contact) const
{
	btVector3 p = origin + direction * length;
	btVector3 n = -direction;
	btScalar d = length;
	int patch_id = -1;
	const BEZIER * b = 0;
	const TRACKSURFACE * s = TRACKSURFACE::None();
	btCollisionObject * c = 0;
	
	MyRayResultCallback ray(origin, p, caster);
	world.rayTest(origin, p, ray);
	
	// track geometry collision
	bool geometryHit = ray.hasHit();
	if (geometryHit)
	{
		p = ray.m_hitPointWorld;
		n = ray.m_hitNormalWorld;
		d = ray.m_closestHitFraction * length;
		c = ray.m_collisionObject;
		if (c->isStaticObject())
		{
			void * ptr = c->getUserPointer();
			if(ptr != 0)
			{
				s = static_cast<const TRACKSURFACE * const>(ptr);
			}
			else if (ray.m_shapeId >= 0 && ray.m_shapeId < (int)track->GetTrackObjects().size()) //track geometry
			{
				s = track->GetTrackObjects()[ray.m_shapeId].surface;
			}
		}
		
		// track bezierpatch collision
		if (track)
		{
			MATHVECTOR <float, 3> bezierspace_raystart(origin[1], origin[2], origin[0]);
			MATHVECTOR <float, 3> bezierspace_dir(direction[1], direction[2], direction[0]);
			MATHVECTOR <float, 3> colpoint;
			MATHVECTOR <float, 3> colnormal;
			patch_id = contact.GetPatchId();
			
			if(track->CastRay(bezierspace_raystart, bezierspace_dir, length,
				patch_id, colpoint, b, colnormal))
			{
				p = btVector3(colpoint[2], colpoint[0], colpoint[1]);
				n = btVector3(colnormal[2], colnormal[0], colnormal[1]);
				d = (colpoint - bezierspace_raystart).Magnitude();
			}
		}

		contact = COLLISION_CONTACT(p, n, d, patch_id, b, s, c);
		return true;
	}

	// should only happen on vehicle rollover
	contact = COLLISION_CONTACT(p, n, d, patch_id, b, s, c);
	return false;
}

void COLLISION_WORLD::Update(btScalar dt)
{
	world.stepSimulation(dt, maxSubSteps, timeStep);
	//CProfileManager::dumpAll();
}

void COLLISION_WORLD::DebugPrint(std::ostream & out) const
{
	out << "Collision objects: " << world.getNumCollisionObjects() << std::endl;
}

void COLLISION_WORLD::Clear()
{
	track = 0;
	
	for(int i = 0; i < objects.size(); ++i)
	{
		world.removeCollisionObject(objects[i]);
		delete objects[i];
	}
	objects.resize(0);
	
	for(int i = 0; i < shapes.size(); ++i)
	{
		btCollisionShape * shape = shapes[i];
		if (shape->isCompound())
		{
			btCompoundShape * cs = (btCompoundShape *)shape;
			for (int i = 0; i < cs->getNumChildShapes(); ++i)
			{
				delete cs->getChildShape(i);
			}
		}
		delete shape;
	}
	shapes.resize(0);
	
	for(int i = 0; i < meshes.size(); ++i)
	{
		delete meshes[i];
	}
	meshes.resize(0);
	
	collisionbroadphase.resetPool(&collisiondispatcher);
}
