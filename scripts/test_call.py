#!/usr/bin/env python3

import rospy
from my_abb_msgs.srv import ChangeEGMTool, ChangeEGMToolRequest

def main():
    rospy.init_node("change_tool_client", anonymous=True)

  
    rospy.wait_for_service("/change_egm_tool")

    try:
        
        change_tool_srv = rospy.ServiceProxy("/change_egm_tool", ChangeEGMTool)

   
        req = ChangeEGMToolRequest()
        
        # Example data for a tool like:
        # [TRUE,
        #  [[0,0,0],[1,0,0,0]],
        #  [0.794,[0,0,99.5],[1,0,0,0],0,0,0]]
        req.frame_is_const = True
        
        # TCP position
        req.pos_x = 0.0
        req.pos_y = 0.0
        req.pos_z = 0.0

        # TCP orientation (quaternion)
        req.ori_w = 1.0
        req.ori_x = 0.0
        req.ori_y = 0.0
        req.ori_z = 0.0

        # Load data (mass [kg], center of gravity [mm], inertias if needed)
        req.mass = 0.794
        req.cog_x = 0.0
        req.cog_y = 0.0
        req.cog_z = 99.5
        req.ixx = 0.0
        req.iyy = 0.0
        req.izz = 0.0

        # 4) Call the service
        response = change_tool_srv(req)

        # 5) Check result
        if response.success:
            rospy.loginfo("Tool changed successfully: %s", response.message)
        else:
            rospy.logwarn("Failed to change tool: %s", response.message)
    
    except rospy.ServiceException as e:
        rospy.logerr("Service call failed: %s", e)


if __name__ == "__main__":
    main()
