import { RouteRecordRaw } from "vue-router";

// 定义路由规则
const routeMap: Array<RouteRecordRaw> = [
  {
    path: "/primary",
    name: "primary",
    component: () => import("@views/primary.vue"),
  },
  {
    path: "/frameless-sample",
    name: "frameless-sample",
    component: () => import("@views/frameless-sample.vue"),
  },
  {
    path: "/test-video",
    name: "test-video",
    component: () => import("../../lib/test-video/renderer/test-video-player.vue"),
  }
];

export default routeMap;